#!/usr/bin/env python3
"""Build experiment005 synthetic features/SEPARATE labels from approved local data.

No download/training/input capture. The only child is the existing, isolated
experiment004 decision driver, used for unchanged token baselines. Original004
files are overlaid read-only in that namespace and never changed on the host.
"""
import argparse
import atexit
import ctypes as c
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

PROTOCOL_SHA = '265757d6d538c400c95243996d853a9e71f77f96892c34510d26beb0c3e23e8b'
ORDER = "`qwertyuiop[]asdfghjkl;'zxcvbnm,."
RU = 'абвгдеёжзийклмнопрстуфхцчшщъыьэюя'


def partition(key):
    digest = hashlib.sha256(b'rasklad-SYS-237-pair-v1\0' + key).digest()
    return ('development' if int.from_bytes(digest[:8], 'big') % 100 < 35 else 'holdout'), digest


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def load_verified(root, relative, expected_sha, maximum):
    p = root / relative
    if p.resolve() != p.absolute() or not p.is_file() or p.stat().st_size > maximum:
        raise ValueError('unsafe or oversized source')
    raw = p.read_bytes()
    if digest(raw) != expected_sha:
        raise ValueError('source checksum mismatch')
    return raw


def prepare(root):
    from audit_domain_decisions import xkb_entries
    protocol_raw = (root / 'data/experiment-005-protocol.json').read_bytes()
    if digest(protocol_raw) != PROTOCOL_SHA:
        raise ValueError('protocol changed')
    protocol = json.loads(protocol_raw)
    manifest_path = root / 'data/evaluation-005-sources.json'
    manifest = json.loads(manifest_path.read_text())
    if (manifest['status'] != 'approved-local-synthetic-evaluation' or manifest['release_approved'] is not False
            or manifest['protocol_sha256'] != PROTOCOL_SHA or manifest['features'] is not None):
        raise ValueError('manifest not approved or already prepared')
    out = root / 'build/experiment-005-data'
    out.mkdir(parents=True, exist_ok=True)
    if out.resolve() != out.absolute() or (root / 'build').is_symlink():
        raise ValueError('build boundary differs')
    for name in ['features.tsv', 'labels.jsonl']:
        if (out / name).exists():
            raise ValueError('output exists')
    for notice in manifest['notices']:
        raw = load_verified(root, notice['notice'], notice['notice_sha256'], 1048576)
        (out / Path(notice['notice']).name).write_bytes(raw)
    training_raw = load_verified(root, 'data/sources.json', manifest['sources']['training_manifest_sha256'], 65536)
    morph_raw = load_verified(root, 'data/morphology-003-sources.json', manifest['sources']['morphology_manifest_sha256'], 65536)
    training, morph = json.loads(training_raw), json.loads(morph_raw)
    entries = xkb_entries()
    conversion, canonical = [{}, {}], [{}, {}]
    bases = {e['key']: e['us'] for e in entries if not e['level']}
    for e in entries:
        conversion[0][e['us']] = e['ru']
        conversion[1][e['ru']] = e['us']
        if e['ru'].lower() in RU:
            canonical[0][e['us']] = ORDER.index(bases[e['key']])
            canonical[1][e['ru']] = ORDER.index(bases[e['key']])
    stems = []
    for layout, lang in enumerate(['en', 'ru']):
        source = next(s for s in training['training_sources'] if s['language'] == lang)
        raw = load_verified(root, f'build/experiment-data/{lang}.dic', source['sha256'], 67108864)
        alphabet = set('abcdefghijklmnopqrstuvwxyz' if lang == 'en' else RU)
        words = {line.split('/')[0].split('\t')[0].lower() for line in raw.decode().splitlines()[1:]}
        words = {w for w in words if 1 <= len(w) <= 32 and set(w) <= alphabet}
        stems.append({bytes(canonical[layout][ch] for ch in w) for w in words})
    if [len(x) for x in stems] != [47828, 146229]:
        raise ValueError('stem index differs')
    library = morph['hunspell']
    if digest(Path(library['library_path']).read_bytes()) != library['library_sha256']:
        raise ValueError('library differs')
    lib = c.CDLL(library['library_path'])
    lib.Hunspell_create.argtypes = [c.c_char_p, c.c_char_p]
    lib.Hunspell_create.restype = c.c_void_p
    lib.Hunspell_spell.argtypes = [c.c_void_p, c.c_char_p]
    lib.Hunspell_spell.restype = c.c_int
    lib.Hunspell_destroy.argtypes = [c.c_void_p]
    lib.Hunspell_get_library_version.restype = c.c_char_p
    if lib.Hunspell_get_library_version().decode() != library['version']:
        raise ValueError('library version differs')
    handles = {}
    for i, source in enumerate(morph['languages']):
        raw_aff = load_verified(root, source['aff_path'], source['aff_sha256'], 1048576)
        control = load_verified(root, source['control_aff_path'], source['control_aff_sha256'], 1048576)
        load_verified(root, source['dic_path'], source['dic_sha256'], 67108864)
        derived = b''.join(line for line in raw_aff.splitlines(keepends=True)
                           if not line.split() or line.split()[0] not in [b'PFX', b'SFX'])
        if derived != control:
            raise ValueError('control derivation differs')
        for mode, aff in [('full', source['aff_path']), ('control', source['control_aff_path'])]:
            h = lib.Hunspell_create(str(root / aff).encode(), str(root / source['dic_path']).encode())
            if not h:
                raise ValueError('engine unavailable')
            handles[mode, i] = h
    atexit.register(lambda: [lib.Hunspell_destroy(h) for h in handles.values()])
    def word(key, layout):
        us = ''.join(ORDER[k] for k in key)
        return us if not layout else ''.join(conversion[0][ch] for ch in us)
    def accepted(key, layout, mode='full'):
        return bool(lib.Hunspell_spell(handles[mode, layout], word(key, layout).encode()))
    ambiguous = sorted((k for k in stems[0] & stems[1] if 2 <= len(k) <= 32), key=lambda k: (partition(k)[1], k))
    controls = {}
    for split in ['development', 'holdout']:
        for layout in range(2):
            candidates = sorted((k for k in stems[layout] if 4 <= len(k) <= 12 and partition(k)[0] == split),
                                key=lambda k: (partition(k)[1], k))
            chosen = []
            for key in candidates:
                if accepted(key, layout) and not accepted(key, 1 - layout):
                    chosen.append(key)
                if len(chosen) == 12:
                    break
            if len(chosen) != 12:
                raise ValueError('not enough preregistered controls')
            controls[split, layout] = chosen
    target_keys = set(ambiguous)
    for keys in controls.values():
        target_keys.update(keys)
    probes = [(key, layout) for key in sorted(target_keys) for layout in range(2)]
    neutral = ''.join(f'{layout}\t{["en","ru"][layout]}\tvalid\t{["us","ru"][layout]}\t{word(key,layout)}\t{word(key,layout)}\n' for key, layout in probes).encode()
    (out / 'target-probes.tsv').write_bytes(neutral)
    bridge = {'schema_version': 1, 'status': 'approved-local-evaluation', 'scope': 'local-evaluation-only',
              'release_approved': False, 'notices': manifest['notices'],
              'purpose': 'synthetic token-baseline adapter only, not a new004 natural evaluation',
              'cases': {'path': 'build/experiment-004-data/cases.tsv', 'sha256': digest(neutral),
                        'bytes': len(neutral), 'rows': len(probes)}}
    adapter = root / 'data/.context-005-token-adapter.json'
    if adapter.exists():
        raise ValueError('adapter exists')
    adapter.write_text(json.dumps(bridge))
    prefix = ['bwrap', '--unshare-all', '--die-with-parent', '--new-session', '--clearenv',
              '--ro-bind', '/usr', '/usr', '--symlink', 'usr/lib', '/lib', '--symlink', 'usr/lib', '/lib64',
              '--ro-bind', str(root), '/work', '--ro-bind', str(out / 'target-probes.tsv'), '/work/build/experiment-004-data/cases.tsv',
              '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp', '--setenv', 'HOME', '/tmp',
              '--setenv', 'PATH', '/usr/bin', '--setenv', 'LANG', 'C.UTF-8', '--chdir', '/work']
    try:
        result = subprocess.run(prefix + ['/work/build/tools/domain_decisions', '--protocol', '/work/data/experiment-004-protocol.json',
                                         '--cases-manifest', '/work/data/.context-005-token-adapter.json',
                                         '--morph-manifest', '/work/data/morphology-003-sources.json'],
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode or result.stderr:
            raise ValueError('baseline driver failed')
    finally:
        adapter.unlink()
    (out / 'token-baselines.tsv').write_bytes(result.stdout)
    model_ref = json.loads((root / 'data/experiment-004-protocol.json').read_text())['model']
    blob = load_verified(root, model_ref['path'], model_ref['sha256'], 88290)
    tables = [struct.unpack('44135b', blob[20 + i * 44135:20 + (i + 1) * 44135]) for i in range(2)]
    def model_action(key, layout):
        if len(key) < 4:
            return 'A'
        sums = [0, 0]
        a = b = 33
        for char in list(key) + [34]:
            for i in range(2):
                sums[i] += tables[i][char] + tables[i][35+b*35+char] + tables[i][1260+(a*35+b)*35+char]
            a, b = b, char
        obs, alt = [v / (8*(len(key)+1)) for v in sums][layout], [v / (8*(len(key)+1)) for v in sums][1-layout]
        return 'C' if alt > obs and alt >= -13 and obs <= -9 and alt-obs >= 2.5 else 'K' if obs > -9 and obs >= alt else 'A'
    baseline = {}
    lines = result.stdout.decode().splitlines()
    if len(lines) != len(probes):
        raise ValueError('baseline row count differs')
    for i, ((key, layout), line) in enumerate(zip(probes, lines)):
        ordinal, eligible, amb, rt, codes = line.split('\t')
        obs, alt = accepted(key, layout), accepted(key, 1-layout)
        full_action = ('K' if obs else 'C' if alt else 'A') if len(key) >= 4 else 'A'
        if ordinal != str(i) or codes[1] != model_action(key, layout) or codes[2] != full_action or rt != '1':
            raise ValueError('independent baseline verification differs')
        shared = key in stems[0] and key in stems[1] or obs and alt or accepted(key, layout, 'control') and accepted(key, 1-layout, 'control')
        baseline[key, layout] = [str(int(obs)), str(int(alt)), str(int(shared)), codes[1], codes[2]]
    features, labels = [], []
    def add(key, layout, anchors, field, scenario, gold, group, collision=''):
        split = partition(key)[0]
        row = '\t'.join([["us","ru"][layout], word(key, layout), '', '', *anchors, field, *baseline[key, layout]])
        assert len(row.split('\t')) == 12
        features.append(row + '\n')
        labels.append({'ordinal': len(labels), 'split': split, 'keyGroup': digest(key), 'scenario': scenario,
                       'gold': gold, 'group': group, 'collisionPair': collision, 'layout': ['us','ru'][layout], 'length': len(key)})
    for key in ambiguous:
        split = partition(key)[0];contexts = protocol['selection']['context_groups'][split]
        for layout in range(2):
            for j, anchors in enumerate(contexts[['en','ru'][layout]]):
                other = contexts[['en','ru'][1-layout]][j]
                collision = digest(key + bytes([layout, j]))
                add(key, layout, anchors, 'prose', 'nativeKeep', 'K', 'ambiguous')
                add(key, layout, other, 'prose', 'switchCorrect', 'C', 'ambiguous', collision)
                add(key, layout, other, 'prose', 'borrowKeep', 'K', 'ambiguous', collision)
                add(key, layout, other, 'protected', 'protectedKeep', 'K', 'ambiguous')
                add(key, layout, other, 'unknown', 'unknownKeep', 'K', 'ambiguous')
                add(key, layout, [other[0], anchors[1]], 'prose', 'mixedKeep', 'K', 'ambiguous')
                add(key, layout, ['', ''], 'prose', 'emptyKeep', 'K', 'ambiguous')
    for (split, native_layout), keys in controls.items():
        contexts = protocol['selection']['context_groups'][split][['en','ru'][native_layout]]
        for key in keys:
            for anchors in contexts:
                add(key, native_layout, anchors, 'prose', 'nativeKeep', 'K', 'control')
                for field, scenario, gold in [('prose','switchCorrect','C'),('protected','protectedKeep','K'),('unknown','unknownKeep','K')]:
                    add(key, 1-native_layout, anchors, field, scenario, gold, 'control')
    raw_features = ''.join(features).encode()
    raw_labels = ''.join(json.dumps(label, sort_keys=True) + '\n' for label in labels).encode()
    if len(raw_features) > 8388608 or len(features) > 100000:
        raise ValueError('feature limits exceeded')
    for name, raw, field in [('features.tsv', raw_features, 'features'), ('labels.jsonl', raw_labels, 'labels')]:
        p = out / name;p.write_bytes(raw)
        manifest[field] = {'path': str(p.relative_to(root)), 'sha256': digest(raw), 'bytes': len(raw), 'rows': len(features)}
    manifest['selection_summary'] = {'ambiguityKeys': len(ambiguous), 'ambiguityKeyPartitions': {s:sum(partition(k)[0] == s for k in ambiguous) for s in ['development','holdout']},
                                     'ambiguityLongKeys': sum(len(k)>=4 for k in ambiguous), 'controlKeysPerSplitLanguage':12,
                                     'tokenBaselineRowsIndependentlyMatched':len(probes), 'rows':len(features),
                                     'keyGroupOverlap':0, 'anchorVocabularyOverlap':0}
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
    (out / 'MODIFICATIONS.txt').write_text('rasklad SYS-237 local-only synthetic evaluation. Lowercase stem intersection, deterministic key-group split, original synthetic prefixes, separate intent labels, read-only token baseline adapter, derived features. Preserve all notices. release_approved=false. No endorsement, no personal typing.\n')
    print(json.dumps(manifest['selection_summary']))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    prepare(Path(__file__).resolve().parent.parent)
