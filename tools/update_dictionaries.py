#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""SYS-272: update the pinned LibreOffice dictionaries (maintainers only).

The product ships one reviewed profile pinned by SHA-256 in several places
(data/*.json, src/profileverify.cpp, tools/make_release_profile.py, the notices
and the docs). An update is an ordinary reviewed change made in five steps:

  plan    --commit SHA            what would be downloaded; no network
  fetch   --commit SHA --network  download the dictionaries, their upstream
                                  READMEs and a fresh Tatoeba export into
                                  build/dict-update/<commit12>/download/
                                  (ask the owner before every run)
  build   --commit SHA            control .aff, evaluation sets, model and word
                                  list (detector_benchmark --dictionaries new),
                                  release profile; nothing outside the work dir
  compare --commit SHA            the new dictionaries against the pinned ones,
                                  both on the same fresh export; exits 1 on a
                                  regression beyond the allowed margin
  pin     --commit SHA            writes the new pins into the repository

After `pin`: rebuild, run the tests and the stand on the new profile
(tools/build_arch_package.sh build/dict-update/<commit12>/release), review the
diff, and send it to independent review like any change. A changed upstream
README stops `build` until it is accepted with --accept-notices (the license
review is a person's decision, recorded in the commit).

Downloaded files are untrusted: each fetch gets its own new directory, the files
are read as bytes only, and nothing from them is executed.
"""
import argparse
import bz2
import datetime
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = 'https://raw.githubusercontent.com/LibreOffice/dictionaries/{commit}/{path}'
# Upstream file -> its name in download/. The READMEs are the notices, kept verbatim.
DICTIONARY_FILES = {
    'en/en_US.dic': 'en.dic',
    'en/en_US.aff': 'en.aff',
    'en/README_en_US.txt': 'en_US.README.txt',
    'ru_RU/ru_RU.dic': 'ru.dic',
    'ru_RU/ru_RU.aff': 'ru.aff',
    'ru_RU/README_ru_RU.txt': 'ru_RU.README.txt',
}
NOTICES = ('en_US.README.txt', 'ru_RU.README.txt')
MAX_DOWNLOAD = 64 * 1024 * 1024
TEXT_FILES = ('README.md', 'README.en.md', 'packaging/arch/PROFILE.md', 'packaging/arch/DATA-NOTICE.txt')
MODIFICATIONS = ('Modified numerical artifacts derived locally from reviewed LibreOffice dictionaries. '
                 'Local experiment only, release_approved=false. See data/sources.json and experiment002 '
                 'protocol.\n')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def file_pin(path):
    data = Path(path).read_bytes()
    return sha256(data), len(data)


def commit_arg(value):
    if not re.fullmatch(r'[0-9a-f]{40}', value):
        raise argparse.ArgumentTypeError('a full 40-hex commit id')
    return value


def work_dir(commit):
    if (ROOT / 'build').is_symlink():
        raise SystemExit('build/ is a symlink')
    return ROOT / 'build' / 'dict-update' / commit[:12]


def load_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def dump_json(path, data):
    Path(path).write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def release_tool():
    spec = importlib.util.spec_from_file_location('release_profile', ROOT / 'tools/make_release_profile.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(argv, **kw):
    print('+ ' + ' '.join(str(a) for a in argv), file=sys.stderr)
    return subprocess.run([str(a) for a in argv], check=True, **kw)


def plan(commit):
    rows = [(RAW.format(commit=commit, path=p), name) for p, name in DICTIONARY_FILES.items()]
    for s in load_json(ROOT / 'data/evaluation-002-sources.json')['evaluation_sources']:
        rows.append((s['url'], s['iso_language'] + '_sentences_CC0.tsv.bz2'))
    # The pinned dictionaries too: compare scores old and new on the same export.
    for s in load_json(ROOT / 'data/sources.json')['training_sources']:
        rows.append((s['url'], 'old-%s.dic' % s['language']))
    return rows


class SameHostHttps(urllib.request.HTTPRedirectHandler):
    """A redirect is followed only to https on the same host."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        old, new = urllib.parse.urlsplit(req.full_url), urllib.parse.urlsplit(newurl)
        if new.scheme != 'https' or new.hostname != old.hostname:
            raise SystemExit('refused: redirect from %s to %s' % (req.full_url, newurl))
        return super().redirect_request(req, fp, code, msg, headers, newurl)


# ---- fetch -------------------------------------------------------------------

def download(url, target):
    if not url.startswith('https://'):
        raise SystemExit('refused: not https: ' + url)
    request = urllib.request.Request(url, headers={'User-Agent': 'rasklad-dictionary-update'})
    opener = urllib.request.build_opener(SameHostHttps)
    with opener.open(request, timeout=60) as reply:
        data = reply.read(MAX_DOWNLOAD + 1)
    if len(data) > MAX_DOWNLOAD:
        raise SystemExit('refused: larger than %d bytes: %s' % (MAX_DOWNLOAD, url))
    target.write_bytes(data)
    return {'url': url, 'sha256': sha256(data), 'bytes': len(data)}


def fetch(commit, network):
    if not network:
        raise SystemExit("fetch downloads from the network: pass --network (only with the owner's OK)")
    work = work_dir(commit)
    down = work / 'download'
    if down.exists():
        raise SystemExit('already fetched: %s (remove it to fetch again)' % down)
    down.mkdir(parents=True)
    files = {}
    for url, name in plan(commit):
        files[name] = download(url, down / name)
        print('%s  %9d  %s' % (files[name]['sha256'][:12], files[name]['bytes'], name))
    for s in load_json(ROOT / 'data/sources.json')['training_sources']:
        got = files['old-%s.dic' % s['language']]
        if (got['sha256'], got['bytes']) != (s['sha256'], s['bytes']):
            raise SystemExit('the pinned %s dictionary did not match its pin' % s['language'])
    dump_json(work / 'fetch.json', {'commit': commit, 'date': datetime.date.today().isoformat(),
                                    'files': files})


# ---- build -------------------------------------------------------------------

def changed_notices(down):
    return [n for n in NOTICES if (down / n).read_bytes() != (ROOT / 'data/notices' / n).read_bytes()]


def build(commit, accept_notices):
    work = work_dir(commit)
    down = work / 'download'
    record = load_json(work / 'fetch.json')
    if record['commit'] != commit:
        raise SystemExit('fetch.json is for another commit')
    for name, pin in record['files'].items():
        if file_pin(down / name) != (pin['sha256'], pin['bytes']):
            raise SystemExit('changed since fetch: ' + name)
    changed = changed_notices(down)
    if changed and not accept_notices:
        raise SystemExit('upstream notices changed: %s. Review the license text against data/notices/, '
                         'then run build again with --accept-notices.' % ', '.join(changed))
    for sub in ('profile', 'eval', 'baseline', 'release'):
        if (work / sub).exists():
            raise SystemExit('already built: %s (remove it to build again)' % (work / sub))
    benchmark = ROOT / 'build/tools/detector_benchmark'
    if not benchmark.is_file():
        raise SystemExit('build the tools first: bin/build')

    # The profile is also the repository tree detector_benchmark sees: it
    # resolves each notice next to its manifest (the manifest's directory/..) and
    # takes an artifact directory only inside that tree's build/.
    profile = work / 'profile'
    stage = profile
    (stage / 'data/notices').mkdir(parents=True)
    for name in NOTICES:
        shutil.copyfile(down / name, stage / 'data/notices' / name)
    shutil.copyfile(ROOT / 'data/notices/Tatoeba-CC0-REVIEW.md', stage / 'data/notices/Tatoeba-CC0-REVIEW.md')
    evaluation = load_json(ROOT / 'data/evaluation-002-sources.json')
    licence = evaluation.get('license_notice')
    if licence:
        (stage / licence).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / licence, stage / licence)
    shutil.copyfile(ROOT / 'data/experiment-002-protocol.json', stage / 'data/experiment-002-protocol.json')

    dic = profile / 'build/experiment-data'
    aff = profile / 'build/experiment-003-data'
    arts = profile / 'build/experiment-002-artifacts'
    for d in (dic, aff / 'control', arts):
        d.mkdir(parents=True)
    for lang in ('en', 'ru'):
        shutil.copyfile(down / (lang + '.dic'), dic / (lang + '.dic'))
        shutil.copyfile(down / (lang + '.aff'), aff / (lang + '.aff'))
        run([sys.executable, '-I', ROOT / 'tools/prepare_affix_control.py',
             '--input', aff / (lang + '.aff'), '--output', aff / 'control' / (lang + '.aff')])

    # The training manifest: the pinned one with the new commit, URLs and hashes.
    sources = load_json(ROOT / 'data/sources.json')
    sources['commit'] = commit
    for s in sources['training_sources']:
        s['url'] = RAW.format(commit=commit, path=s['path'])
        s['sha256'], s['bytes'] = file_pin(dic / ('en.dic' if s['language'] == 'en' else 'ru.dic'))
        s['notice_sha256'] = file_pin(stage / s['notice'])[0]
    dump_json(stage / 'data/sources.json', sources)

    # Evaluation: the fresh Tatoeba export, prepared as before, pinned anew.
    eval_dir = work / 'eval'
    eval_dir.mkdir()
    for s in evaluation['evaluation_sources']:
        packed = down / (s['iso_language'] + '_sentences_CC0.tsv.bz2')
        out = eval_dir / (s['language'] + '.tsv')
        run([sys.executable, '-I', ROOT / 'tools/prepare_cc0_evaluation.py',
             '--language', s['iso_language'], '--input', packed, '--output', out])
        upstream = bz2.decompress(packed.read_bytes())
        s['sha256'], s['bytes'] = file_pin(out)
        s['upstream_sha256'], s['upstream_bytes'] = sha256(upstream), len(upstream)
        s['compressed_sha256'], s['compressed_bytes'] = file_pin(packed)
        s['rows'] = out.read_bytes().count(b'\n')
    evaluation['snapshot_date'] = record['date']
    dump_json(stage / 'data/evaluation-002-sources.json', evaluation)

    with open(work / 'report.json', 'wb') as report:
        run([benchmark, '--manifest', stage / 'data/sources.json',
             '--en', dic / 'en.dic', '--ru', dic / 'ru.dic',
             '--eval-manifest', stage / 'data/evaluation-002-sources.json',
             '--eval-en', eval_dir / 'en.tsv', '--eval-ru', eval_dir / 'ru.tsv',
             '--protocol', stage / 'data/experiment-002-protocol.json',
             '--artifact-dir', arts, '--dictionaries', 'new', '--json'], stdout=report)

    # Baseline: the pinned dictionaries on the same fresh export, in a tree of
    # their own (the pinned manifest and notices).
    base = work / 'baseline'
    (base / 'data/notices').mkdir(parents=True)
    (base / 'build').mkdir()
    for rel in ('data/sources.json', 'data/experiment-002-protocol.json', 'data/notices/Tatoeba-CC0-REVIEW.md') + \
            tuple('data/notices/' + n for n in NOTICES) + ((licence,) if licence else ()):
        (base / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / rel, base / rel)
    shutil.copyfile(stage / 'data/evaluation-002-sources.json', base / 'data/evaluation-002-sources.json')
    with open(work / 'baseline-report.json', 'wb') as report:
        run([benchmark, '--manifest', base / 'data/sources.json',
             '--en', down / 'old-en.dic', '--ru', down / 'old-ru.dic',
             '--eval-manifest', base / 'data/evaluation-002-sources.json',
             '--eval-en', eval_dir / 'en.tsv', '--eval-ru', eval_dir / 'ru.tsv',
             '--protocol', base / 'data/experiment-002-protocol.json', '--json'], stdout=report)

    for name in NOTICES:
        shutil.copyfile(down / name, arts / name.replace('README', 'NOTICE'))
    (arts / 'MODIFICATIONS.txt').write_text(MODIFICATIONS, encoding='utf-8')

    source_pins = {rel: list(file_pin(profile / rel)) for rel in release_tool().SOURCE_PINS}
    dump_json(work / 'source-pins.json', source_pins)
    run([sys.executable, '-I', ROOT / 'tools/make_release_profile.py', '--from', profile,
         '--out', work / 'release', '--source-pins', work / 'source-pins.json', '--commit', commit],
        stdout=subprocess.DEVNULL)
    print('built: %s' % (work / 'release'))


# ---- compare -----------------------------------------------------------------

# The product's method (exactFullThenTwoScore) and its headline rates, overall and
# per language, by occurrence and by unique word: (rate, lower is better, margin).
METHOD = 'exactFullThenTwoScore'
HEADLINE = (('valid/falseChangeRate', True, 0.0005),      # a correct word changed
            ('wrong/recoveryRate', False, 0.01),          # a mistyped word fixed
            ('ambiguousValid/changeRate', True, 0.0),     # never change an ambiguous word
            ('ambiguousWrong/changeRate', True, 0.0))
SCOPES = ('', 'byIntendedLanguage/en/', 'byIntendedLanguage/ru/')


def rates(report):
    """{name: (value, lower is better, margin)} for the headline rates."""
    method = report.get('methods', {}).get(METHOD, {})
    out = {}
    for unit in ('occurrences', 'uniqueTypes'):
        for scope in SCOPES:
            for rate, lower, margin in HEADLINE:
                node = method.get(unit, {})
                for part in (scope + rate).split('/'):
                    node = node.get(part) if isinstance(node, dict) else None
                if isinstance(node, (int, float)) and not isinstance(node, bool):
                    out['%s/%s/%s%s' % (METHOD, unit, scope, rate)] = (float(node), lower, margin)
    return out


def worse(old, new, lower, margin):
    return (new - old > margin) if lower else (old - new > margin)


def compare(commit):
    # Both runs score the same fresh export: only the dictionaries differ.
    old = rates(load_json(work_dir(commit) / 'baseline-report.json'))
    new = rates(load_json(work_dir(commit) / 'report.json'))
    if len(old) != len(SCOPES) * len(HEADLINE) * 2:
        raise SystemExit('compare: the baseline report lacks headline rates')
    failed = []
    for name, (value, lower, margin) in sorted(old.items()):
        if name not in new:
            failed.append(name)
            print('%-70s %9.5f  missing' % (name, value))
            continue
        bad = worse(value, new[name][0], lower, margin)
        if bad:
            failed.append(name)
        print('%-70s %9.5f %9.5f %s' % (name, value, new[name][0], 'WORSE' if bad else ''))
    if failed:
        print('quality: %d headline rates worse beyond their margin (or missing): do not pin' % len(failed))
        return 1
    print('quality: no headline rate worse beyond its margin')
    return 0


# ---- pin ---------------------------------------------------------------------

def removed_lines(aff):
    return sum(1 for line in Path(aff).read_bytes().splitlines() if line.lstrip().startswith((b'PFX', b'SFX')))


def cpp_pin(text, rel):
    m = re.search(r'QStringLiteral\("%s"\),\s*QStringLiteral\("([0-9a-f]{64})"\), (\d+)' % re.escape(rel), text)
    if not m:
        raise SystemExit('pin: %s not found in src/profileverify.cpp' % rel)
    return m.group(1), int(m.group(2))


def pin_cpp(text, rel, digest, size, where):
    """Replace the pin of `rel` in profileverify.cpp: {QStringLiteral("rel"), QStringLiteral("hash")[, bytes]}."""
    pattern = re.compile(r'(QStringLiteral\("%s"\),\s*QStringLiteral\(")[0-9a-f]{64}("\))(, \d+)?'
                         % re.escape(rel))
    found = pattern.findall(text)
    if len(found) != 1:
        raise SystemExit('pin: %s appears %d times in %s' % (rel, len(found), where))

    def put(m):
        tail = m.group(3)
        if tail is None or tail == ', 0':
            return m.group(1) + digest + m.group(2) + (tail or '')
        return m.group(1) + digest + m.group(2) + ', %d' % size
    return pattern.sub(put, text)


def pin(commit, root=ROOT):
    """Writes the pins into `root` (the repository; a copy in the tests)."""
    work = work_dir(commit)
    profile, release = work / 'profile', work / 'release'
    stage = profile  # the manifests and notices as built
    old_commit = load_json(root / 'data/sources.json')['commit']
    if old_commit == commit:
        raise SystemExit('already pinned to this commit')

    # 1. Notices and manifests (the manifests' own hashes are pinned in step 3).
    for name in NOTICES:
        shutil.copyfile(stage / 'data/notices' / name, root / 'data/notices' / name)
    shutil.copyfile(stage / 'data/sources.json', root / 'data/sources.json')
    shutil.copyfile(stage / 'data/evaluation-002-sources.json', root / 'data/evaluation-002-sources.json')
    for manifest, controls in (('data/morphology-003-sources.json', profile),
                               ('data/product-profile-sources.json', release)):
        data = load_json(root / manifest)
        data['commit'] = commit
        for lang in data['languages']:
            upstream = 'en/en_US.aff' if lang['language'] == 'en' else 'ru_RU/ru_RU.aff'
            lang['upstream_url'] = RAW.format(commit=commit, path=upstream)
            lang['dic_sha256'], lang['dic_bytes'] = file_pin(profile / lang['dic_path'])
            lang['aff_sha256'], lang['aff_bytes'] = file_pin(profile / lang['aff_path'])
            lang['control_aff_sha256'], lang['control_aff_bytes'] = file_pin(controls / lang['control_aff_path'])
            lang['notice_sha256'] = file_pin(root / lang['notice'])[0]
            lang['removedPfxSfxLines'] = removed_lines(profile / lang['aff_path'])
        dump_json(root / manifest, data)

    # 2. The release tool: its source pins and commit.
    tool = root / 'tools/make_release_profile.py'
    text = tool.read_text(encoding='utf-8')
    for rel, (digest, size) in load_json(work / 'source-pins.json').items():
        text, n = re.subn(r"('%s': \(')[0-9a-f]{64}(', )\d+(\))" % re.escape(rel),
                          lambda m: m.group(1) + digest + m.group(2) + str(size) + m.group(3), text)
        if n != 1:
            raise SystemExit('pin: %s appears %d times in %s' % (rel, n, tool))
    old = "DICTIONARY_COMMIT = '%s'" % old_commit
    if text.count(old) != 1:
        raise SystemExit('pin: DICTIONARY_COMMIT not found in %s' % tool)
    tool.write_text(text.replace(old, "DICTIONARY_COMMIT = '%s'" % commit), encoding='utf-8')

    # 3. The addon: every pinned file of the release profile, the manifests'
    #    and notices' own hashes, and the commit.
    verify = root / 'src/profileverify.cpp'
    text = verify.read_text(encoding='utf-8')
    model_rel = 'build/experiment-002-artifacts/detector-model.rskn'
    stem_rel = 'build/experiment-002-artifacts/exact-full-lexicons.rskp'
    before = {rel: cpp_pin(text, rel) for rel in (model_rel, stem_rel)}
    pinned = {rel: root for rel in ('data/product-profile-sources.json', 'data/sources.json',
                                    'data/experiment-002-protocol.json',
                                    'data/notices/en_US.README.txt', 'data/notices/ru_RU.README.txt')}
    for rel in release_tool().SOURCE_PINS:
        if rel.endswith(('.dic', '.aff', '.rskn', '.rskp')):
            pinned[rel] = release
    for rel, base in pinned.items():
        text = pin_cpp(text, rel, *file_pin(base / rel), verify)
    old = 'p.dictionaryCommit = QStringLiteral("%s");' % old_commit
    if text.count(old) != 1:
        raise SystemExit('pin: dictionaryCommit not found in %s' % verify)
    verify.write_text(text.replace(old, 'p.dictionaryCommit = QStringLiteral("%s");' % commit), encoding='utf-8')
    # The app tests repeat the model and word-list pins.
    tests = root / 'tests/app/app_tests.cpp'
    text = tests.read_text(encoding='utf-8')
    for rel, (old_hash, old_size) in before.items():
        new_hash, new_size = file_pin(release / rel)
        pair = 'QStringLiteral("%s"));\n        QCOMPARE(p.%s.bytes, qint64(%d));'
        name = 'model' if rel == model_rel else 'stem'
        if text.count(pair % (old_hash, name, old_size)) != 1:
            raise SystemExit('pin: the %s pin not found in %s' % (name, tests))
        text = text.replace(pair % (old_hash, name, old_size), pair % (new_hash, name, new_size))
    tests.write_text(text, encoding='utf-8')

    # 4. The commit in the docs and the package notice (full id and 12-hex form).
    for rel in TEXT_FILES:
        path = root / rel
        text = path.read_text(encoding='utf-8')
        path.write_text(text.replace(old_commit, commit).replace(old_commit[:12], commit[:12]), encoding='utf-8')

    # 5. The new quality report beside the pinned one.
    shutil.copyfile(work / 'report.json', root / 'docs/results' / ('dictionary-update-%s.json' % commit[:12]))
    print('pinned %s. Next: bin/build, the tests, tools/build_arch_package.sh %s, the stand; '
          'then review git diff and send it to independent review.' % (commit, release))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('step', choices=('plan', 'fetch', 'build', 'compare', 'pin'))
    ap.add_argument('--commit', type=commit_arg, required=True)
    ap.add_argument('--network', action='store_true', help='fetch: allow the download')
    ap.add_argument('--accept-notices', action='store_true', help='build: the changed upstream notices were reviewed')
    args = ap.parse_args()
    if args.step == 'plan':
        for url, name in plan(args.commit):
            print('%-30s %s' % (name, url))
        print('into %s/download/' % work_dir(args.commit))
    elif args.step == 'fetch':
        fetch(args.commit, args.network)
    elif args.step == 'build':
        build(args.commit, args.accept_notices)
    elif args.step == 'compare':
        return compare(args.commit)
    else:
        pin(args.commit)
    return 0


if __name__ == '__main__':
    sys.exit(main())
