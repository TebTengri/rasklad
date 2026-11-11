#!/usr/bin/env python3
"""Aggregate experiment004 case actions without publishing source tokens.

Consumes only an explicit approved manifest and developer-only action TSV.
No threshold fitting, source retrieval, model training, or input integration.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path

METHODS = ['alwaysKeep', 'twoScore', 'hunspellFull', 'modelConfirmedMorph', 'morphThenModel',
           'protectedTwoScore', 'protectedHunspellFull', 'protectedConfirmedMorph', 'protectedMorphThenModel']
PROTOCOL_SHA = '9e1c9a17f9eb1ca1eb1f19643ae987bf98e2ed610611d12aaf2168964cf603e2'


def outcome(role, ambiguous, eligible, action):
    category = 'ambiguousWrong' if role == 'wrong' and ambiguous else role
    c = Counter(count=1, eligible=int(eligible))
    c[action] += 1
    if action == 'C':
        c['changes'] += 1
        c['eligibleChanges'] += int(eligible)
        if role == 'wrong' and not ambiguous:
            c['recovered'] += 1
            c['eligibleRecovered'] += int(eligible)
        else:
            c['unsafe'] += 1
    return category, c


def rates(group):
    result = {role: dict(group.get(role, {})) for role in ['valid', 'protected', 'wrong', 'ambiguousWrong']}
    for x in result.values():
        for key in ['count', 'eligible', 'changes', 'eligibleChanges', 'recovered', 'eligibleRecovered', 'unsafe', 'K', 'C', 'A']:
            x.setdefault(key, 0)
        x['falsePer10000Eligible'] = (10000 * x['eligibleChanges'] / x['eligible']) if x['eligible'] else None
    wrong = result['wrong']
    wrong['eligibleRecoveryRate'] = wrong['eligibleRecovered'] / wrong['eligible'] if wrong['eligible'] else None
    result['unsafeTotal'] = sum(result[r]['unsafe'] for r in ['valid', 'protected', 'ambiguousWrong'])
    return result


def screen(occ, types):
    v, t, wrong = occ['valid'], types['valid'], occ['wrong']
    if not v['eligible'] or not t['eligible'] or not wrong['eligible']:
        return {'status': 'insufficient'}
    checks = {'validOccurrenceBudget': v['eligibleChanges'] * 10000 <= v['eligible'],
              'validTypeBudget': t['eligibleChanges'] * 10000 <= t['eligible'],
              'zeroProtectedChanges': occ['protected']['changes'] == 0,
              'wrongRecoveryAtLeastHalf': wrong['eligibleRecovered'] * 2 >= wrong['eligible']}
    return {'status': 'pass' if all(checks.values()) else 'fail', 'checks': checks}


def summarize(root, manifest, decisions):
    if (manifest.get('status') != 'approved-local-evaluation'
            or manifest.get('scope') != 'local-evaluation-only'
            or manifest.get('release_approved') is not False):
        raise ValueError('manifest is not approved')
    if (decisions_path.stat().st_size > 32 * 1024 * 1024
            or decisions_path.resolve() != decisions_path.absolute()
            or not decisions_path.absolute().is_relative_to(root / 'build')):
        raise ValueError('decision input outside boundary')
    p = (root / 'data/experiment-004-protocol.json').read_bytes()
    if hashlib.sha256(p).hexdigest() != PROTOCOL_SHA or manifest['protocol_sha256'] != PROTOCOL_SHA:
        raise ValueError('protocol changed')
    ref = manifest['cases']
    case_path = root / ref['path']
    if not ref['path'].startswith('build/') or case_path.resolve() != case_path.absolute():
        raise ValueError('case path outside boundary')
    if case_path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError('case size bound exceeded')
    raw = case_path.read_bytes()
    if len(raw) != ref['bytes'] or hashlib.sha256(raw).hexdigest() != ref['sha256']:
        raise ValueError('case checksum mismatch')
    rows = raw.decode('utf-8').splitlines()
    actions = decisions.read_text().splitlines()
    if len(rows) != ref['rows'] or len(actions) != len(rows):
        raise ValueError('row count mismatch')
    # Group by method, scope(language or pooled/doc index), unit, role.
    counts = defaultdict(lambda: defaultdict(lambda: defaultdict(lambda: defaultdict(Counter))))
    seen = set()
    seen_doc = set()
    pairs = defaultdict(Counter)
    for i, (line, output) in enumerate(zip(rows, actions)):
        doc, language, role, layout, text, expected = line.split('\t')
        ordinal, eligible, ambiguous, rt, codes = output.split('\t')
        if (ordinal != str(i) or eligible not in ['0', '1'] or ambiguous not in ['0', '1']
                or rt != '1' or len(codes) != len(METHODS) or set(codes) - set('KCA')):
            raise ValueError('invalid decisions')
        eligible, ambiguous = eligible == '1', ambiguous == '1'
        key = (language, role, layout, text)
        fresh = key not in seen
        fresh_doc = (doc, key) not in seen_doc
        seen.add(key)
        seen_doc.add((doc, key))
        for j, method in enumerate(METHODS):
            category, delta = outcome(role, ambiguous, eligible, codes[j])
            for scope in ['pooled', language, 'doc' + doc]:
                counts[method][scope]['occurrences'][category].update(delta)
                include_type = fresh_doc if scope.startswith('doc') else fresh
                if include_type:
                    counts[method][scope]['types'][category].update(delta)
        # Guarded/base equality is a protocol invariant, independent of quality.
        if role != 'protected' and codes[5:] != codes[1:5]:
            raise ValueError('protection changed prose')
        if role == 'protected' and codes[5:] != 'KKKK':
            raise ValueError('protected gate failed')
        for candidate, base in [(3, 2), (4, 2), (5, 1), (6, 2), (7, 3), (8, 4)]:
            for unit, include in [('occurrences', True), ('types', fresh)]:
                if not include:
                    continue
                pair = pairs[(METHODS[candidate], METHODS[base], unit)]
                recovery_case = role == 'wrong' and not ambiguous
                unsafe_case = role != 'wrong' or ambiguous
                a, b = codes[candidate] == 'C', codes[base] == 'C'
                pair['addedRecoveries'] += int(recovery_case and a and not b)
                pair['lostRecoveries'] += int(recovery_case and b and not a)
                pair['addedUnsafe'] += int(unsafe_case and a and not b)
                pair['preventedUnsafe'] += int(unsafe_case and b and not a)
    report = {'experiment': 'SYS-236 experiment004', 'protocolSha256': PROTOCOL_SHA,
              'cases': {k: ref[k] for k in ['sha256', 'bytes', 'rows']},
              'actionSha256': hashlib.sha256(decisions.read_bytes()).hexdigest(),
              'sourceDocuments': len(manifest['documents']), 'methods': {}, 'pairwise': {},
              'limitations': ['oracle script layout and Markdown protection; not runtime safe-field detection',
                              'technical textbook/translated topics, not representative user typing',
                              'synthetic wrong copies; no deployment precision or IID confidence inferred',
                              'types retain exact case and document language/role/layout',
                              'budget is a frozen research screen, not safety approval']}
    for method in METHODS:
        result = report['methods'][method] = {}
        for scope, units in counts[method].items():
            result[scope] = {u: rates(v) for u, v in units.items()}
            result[scope]['screen'] = screen(result[scope]['occurrences'], result[scope]['types'])
        result['allLanguageScreensPass'] = all(result[x]['screen']['status'] == 'pass' for x in ['en', 'ru', 'pooled'])
        docs = [result['doc' + str(i)] for i in range(40)]
        for role in ['valid', 'protected']:
            result['documentsWith' + role.title() + 'Changes'] = sum(d['occurrences'][role]['changes'] > 0 for d in docs)
        for language in ['en', 'ru']:
            indices = [d['index'] for d in manifest['documents'] if d['language'] == language]
            values = [result['doc' + str(i)]['occurrences']['valid']['falsePer10000Eligible'] for i in indices]
            values = [v for v in values if v is not None]
            result[language]['documentMacroFalsePer10000Eligible'] = sum(values) / len(values) if values else None
    for (candidate, base, unit), c in pairs.items():
        report['pairwise'].setdefault(candidate + ' vs ' + base, {})[unit] = dict(c)
    # File-content duplicate count across source languages, without exporting IDs/text.
    by_lang = {lang: {d['sha256'] for d in manifest['documents'] if d['language'] == lang} for lang in ['en', 'ru']}
    report['crossLanguageExactDocumentDuplicates'] = len(by_lang['en'] & by_lang['ru'])
    report['oldTatoebaOverlap'] = old_overlap(root, rows)
    return report


def old_overlap(root, rows):
    # Diagnostic only, never excludes cases or alters policy. All snapshots002/003.
    trim = '.!?;:,()[]{}"«»“”„'
    previous = {'en': set(), 'ru': set()}
    for language in previous:
        path = root / f'build/experiment-002-data/{language}.tsv'
        # Pin the old source bytes through their original manifest.
        old = json.loads((root / 'data/evaluation-002-sources.json').read_text())
        ref = next(x for x in old['evaluation_sources'] if x['language'] == language)
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != ref['sha256']:
            raise ValueError('old overlap source changed')
        texts = {}
        for row in raw.decode().splitlines():
            sid, iso, text = row.split('\t')
            texts[text] = min(int(sid), texts.get(text, int(sid)))
        ranked = sorted(texts, key=lambda text: (
            hashlib.sha256(b'rasklad-SYS-233-holdout-v1\0' + str(texts[text]).encode()).digest(), texts[text]))[:10000]
        for text in ranked:
            previous[language].update(t.strip(trim) for t in text.split() if t.strip(trim))
    result = {}
    for language in previous:
        words = [r.split('\t')[4] for r in rows if r.split('\t')[1:3] == [language, 'valid']]
        types = set(words)
        result[language] = {'validOccurrences': len(words), 'validTypes': len(types),
                            'occurrencesWithExactTypeInOld': sum(t in previous[language] for t in words),
                            'typesAlsoInOld': len(types & previous[language])}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--decisions', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = Path(args.output).absolute()
    if output.exists() or output.resolve() != output or not output.is_relative_to(root / 'build'):
        raise ValueError('output must be new file inside build')
    report = summarize(root, json.loads(Path(args.manifest).read_text()), Path(args.decisions))
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print('Aggregate report written; no source text exported.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, UnicodeError, TypeError):
        raise SystemExit('summarize_technical_evaluation: validation failed')
