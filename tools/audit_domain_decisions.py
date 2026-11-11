#!/usr/bin/env python3
"""Independent experiment004 routing audit using system XKB and ctypes Hunspell.

Same Hunspell backend, independent mappings, model scoring and policy routing.
No source text printed. Developer-only; no fitting and no system input reads.
"""
import argparse
import atexit
from collections import Counter
import ctypes as c
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import struct


def xkb_entries():
    x = c.CDLL('libxkbcommon.so.0')
    class Names(c.Structure):
        _fields_ = [(s, c.c_char_p) for s in ['rules', 'model', 'layout', 'variant', 'options']]
    x.xkb_context_new.argtypes = [c.c_int]
    x.xkb_context_new.restype = c.c_void_p
    x.xkb_keymap_new_from_names.argtypes = [c.c_void_p, c.POINTER(Names), c.c_int]
    x.xkb_keymap_new_from_names.restype = c.c_void_p
    x.xkb_keymap_key_by_name.argtypes = [c.c_void_p, c.c_char_p]
    x.xkb_keymap_key_by_name.restype = c.c_uint32
    x.xkb_keymap_key_get_syms_by_level.argtypes = [c.c_void_p, c.c_uint32, c.c_uint32, c.c_uint32, c.POINTER(c.POINTER(c.c_uint32))]
    x.xkb_keymap_key_get_syms_by_level.restype = c.c_int
    x.xkb_keysym_to_utf32.argtypes = [c.c_uint32]
    x.xkb_keysym_to_utf32.restype = c.c_uint32
    x.xkb_keymap_unref.argtypes = [c.c_void_p]
    x.xkb_context_unref.argtypes = [c.c_void_p]
    ctx = x.xkb_context_new(0)
    names = Names(b'evdev', b'pc105', b'us,ru', b',', b'')
    keymap = x.xkb_keymap_new_from_names(ctx, c.byref(names), 0)
    if not ctx or not keymap:
        raise ValueError('reference keymap unavailable')
    entries = []
    keys = ['TLDE'] + [f'{row}{i:02d}' for row, n in [('AE', 12), ('AD', 12), ('AC', 11), ('AB', 10)] for i in range(1, n + 1)] + ['BKSL']
    try:
        for key in keys:
            code = x.xkb_keymap_key_by_name(keymap, key.encode())
            for level in range(2):
                chars = []
                for layout in range(2):
                    syms = c.POINTER(c.c_uint32)()
                    if x.xkb_keymap_key_get_syms_by_level(keymap, code, layout, level, c.byref(syms)) != 1:
                        raise ValueError('reference keymap cardinality differs')
                    chars.append(chr(x.xkb_keysym_to_utf32(syms[0])))
                entries.append({'key': key, 'level': level, 'us': chars[0], 'ru': chars[1]})
    finally:
        x.xkb_keymap_unref(keymap)
        x.xkb_context_unref(ctx)
    return entries


def audit(root, decisions_path):
    entries = xkb_entries()
    order = "`qwertyuiop[]asdfghjkl;'zxcvbnm,."
    ru = 'абвгдеёжзийклмнопрстуфхцчшщъыьэюя'
    bases = {e['key']: e['us'] for e in entries if e['level'] == 0}
    mapping, convert = [{}, {}], [{}, {}]
    for e in entries:
        convert[0][e['us']] = e['ru']
        convert[1][e['ru']] = e['us']
        if e['ru'].lower() in ru:
            mapping[0][e['us']] = order.index(bases[e['key']])
            mapping[1][e['ru']] = order.index(bases[e['key']])
    def canonical(text, layout):
        if not 1 <= len(text.encode('utf-16-le')) // 2 <= 32 or any(ch not in mapping[layout] for ch in text):
            return None
        return bytes(mapping[layout][ch] for ch in text)
    full = []
    for language, layout in [('en', 0), ('ru', 1)]:
        alphabet = set('abcdefghijklmnopqrstuvwxyz' if language == 'en' else ru)
        words = {line.split('/')[0].split('\t')[0].lower() for line in (root / f'build/experiment-data/{language}.dic').read_text().splitlines()[1:]}
        words = {w for w in words if 1 <= len(w) <= 32 and set(w) <= alphabet}
        full.append({canonical(w, layout) for w in words})
    if [len(x) for x in full] != [47828, 146229] or any(None in x for x in full):
        raise ValueError('reference stem baseline differs')
    blob = (root / 'build/experiment-002-artifacts/detector-model.rskn').read_bytes()
    if hashlib.sha256(blob).hexdigest() != 'e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8':
        raise ValueError('reference model checksum differs')
    tables = [struct.unpack('44135b', blob[20 + i * 44135:20 + (i + 1) * 44135]) for i in range(2)]
    @lru_cache(None)
    def scores(keys):
        sums = [0, 0]
        a = b = 33
        for ch in list(keys) + [34]:
            for i in range(2):
                sums[i] += tables[i][ch] + tables[i][35 + b * 35 + ch] + tables[i][1260 + (a * 35 + b) * 35 + ch]
            a, b = b, ch
        return [v / (8 * (len(keys) + 1)) for v in sums]
    manifest = json.loads((root / 'data/morphology-003-sources.json').read_text())
    libpath = manifest['hunspell']['library_path']
    if hashlib.sha256(Path(libpath).read_bytes()).hexdigest() != manifest['hunspell']['library_sha256']:
        raise ValueError('reference library checksum differs')
    lib = c.CDLL(libpath)
    lib.Hunspell_create.argtypes = [c.c_char_p, c.c_char_p]
    lib.Hunspell_create.restype = c.c_void_p
    lib.Hunspell_spell.argtypes = [c.c_void_p, c.c_char_p]
    lib.Hunspell_spell.restype = c.c_int
    lib.Hunspell_destroy.argtypes = [c.c_void_p]
    handles = {}
    for mode in ['full', 'control']:
        for i, source in enumerate(manifest['languages']):
            aff = source['aff_path'] if mode == 'full' else source['control_aff_path']
            h = lib.Hunspell_create(str(root / aff).encode(), str(root / source['dic_path']).encode())
            if not h:
                raise ValueError('reference engine unavailable')
            handles[mode, i] = h
    atexit.register(lambda: [lib.Hunspell_destroy(h) for h in handles.values()])
    @lru_cache(None)
    def accepts(mode, language, text):
        return bool(lib.Hunspell_spell(handles[mode, language], text.encode()))
    m = json.loads((root / 'data/evaluation-004-sources.json').read_text())
    raw = (root / m['cases']['path']).read_bytes()
    if hashlib.sha256(raw).hexdigest() != m['cases']['sha256']:
        raise ValueError('reference cases checksum differs')
    rows = raw.decode().splitlines()
    decisions = decisions_path.read_text().splitlines()
    if len(rows) != len(decisions) or len(rows) != m['cases']['rows']:
        raise ValueError('reference row count differs')
    correct_counts = Counter()
    facts = Counter()
    seen = set()
    for i, (row, actual) in enumerate(zip(rows, decisions)):
        doc, language, role, source, text, expected = row.split('\t')
        layout = 0 if source == 'us' else 1
        keys = canonical(text, layout)
        eligible = keys is not None and len(keys) >= 4
        ambiguous = keys is not None and keys in full[0] and keys in full[1]
        if eligible:
            alttext = ''.join(convert[layout].get(ch, ch) for ch in text)
            control = [accepts('control', layout, text), accepts('control', 1 - layout, alttext)]
            morph = [accepts('full', layout, text), accepts('full', 1 - layout, alttext)]
            ambiguous = ambiguous or all(control) or all(morph)
            obs, alt = scores(keys)[layout], scores(keys)[1 - layout]
            model = 'C' if alt > obs and alt >= -13 and obs <= -9 and alt - obs >= 2.5 else 'K' if obs > -9 and obs >= alt else 'A'
            exact = 'K' if morph[0] else 'C' if morph[1] else 'A'
            confirmed = 'K' if morph[0] else 'C' if morph[1] and model == 'C' else 'A'
            fallback = exact if exact != 'A' else model
            codes = 'K' + model + exact + confirmed + fallback
        else:
            codes = 'KAAAA'
        codes += 'KKKK' if role == 'protected' else codes[1:5]
        if role == 'wrong' and ''.join(convert[layout].get(ch, ch) for ch in text) != expected:
            raise ValueError('reference wrong roundtrip differs')
        wanted = f'{i}\t{int(eligible)}\t{int(ambiguous)}\t1\t{codes}'
        if actual != wanted:
            raise ValueError('reference decision mismatch at ordinal ' + str(i))
        sig = (language, role, source, text)
        fresh = sig not in seen
        seen.add(sig)
        for method, code in enumerate(codes):
            correct_counts[method] += code == 'C'
            for scope in [language, 'pooled']:
                for unit, include in [('occurrences', True), ('types', fresh)]:
                    if not include:
                        continue
                    for field, value in [
                        ('validEligible', role == 'valid' and eligible),
                        ('validFalse', role == 'valid' and code == 'C'),
                        ('validEligibleFalse', role == 'valid' and eligible and code == 'C'),
                        ('codeFalse', role == 'protected' and code == 'C'),
                        ('wrongEligible', role == 'wrong' and not ambiguous and eligible),
                        ('recovered', role == 'wrong' and not ambiguous and code == 'C'),
                        ('eligibleRecovered', role == 'wrong' and not ambiguous and eligible and code == 'C'),
                        ('ambiguousUnsafe', role == 'wrong' and ambiguous and code == 'C')]:
                        facts[method, scope, unit, field] += int(value)
    return {'rowsMatched': len(rows), 'ninePoliciesMatched': True,
            'oracleSha256': hashlib.sha256(json.dumps(entries, sort_keys=True).encode()).hexdigest(),
            'correctActionsByPolicyIndex': dict(correct_counts),
            'primaryFacts': {'|'.join(map(str, k)): v for k, v in facts.items()},
            'scope': 'independent system XKB mapping, scoring/routing; same Hunspell engine, not an independent linguistic backend'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decisions', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    print(json.dumps(audit(root, Path(args.decisions)), indent=2))


if __name__ == '__main__':
    main()
