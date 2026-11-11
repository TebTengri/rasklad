#!/usr/bin/env python3
"""Independently check context005 policies and aggregate separate intent budgets.

Consumes explicit local feature, label and action files. Does not fit rules,
train models, read input devices or publish text. Counts are synthetic, not
natural error rates. Labels are never fed to C++ policy decisions.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path

SHA = '265757d6d538c400c95243996d853a9e71f77f96892c34510d26beb0c3e23e8b'
METHODS = ['alwaysKeep', 'tokenTwoScore', 'tokenHunspell', 'contextAuto', 'guardedContextAuto', 'contextSuggest']
RU = set('абвгдеёжзийклмнопрстуфхцчшщъыьэюя')
EN = set('abcdefghijklmnopqrstuvwxyz')
US_KEYS = set("`qwertyuiop[]asdfghjkl;'zxcvbnm,.~QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>")
RU_KEYS = RU | {ch.upper() for ch in RU}


def script(word):
    if not 4 <= len(word) <= 32:
        return '-'
    chars = set(word.lower())
    return 'U' if chars <= EN else 'R' if chars <= RU else '-'


def reference(fields):
    layout, target, a, b, left, right, field, obs, alt, ambiguous, model, full = fields
    v1, v2 = script(left), script(right)
    vote = v1 if v1 == v2 else '-'
    native_keys = US_KEYS if layout == 'us' else RU_KEYS
    supported = 2 <= len(target) <= 32 and set(target) <= native_keys
    if field == 'protected':
        action = guarded = 'K'
    elif field == 'unknown' or not supported or vote == '-':
        action = guarded = 'A'
    else:
        same = vote == ('U' if layout == 'us' else 'R')
        action = 'K' if obs == '1' and alt == '0' else ('K' if same and obs == '1' else 'C' if not same else 'A') if alt == '1' else 'A'
        guarded = 'A' if ambiguous == '1' else action
    suggest = 'S' if action == 'C' else action
    return vote, 'K' + model + full + action + guarded + suggest


def classify(label):
    if label['gold'] == 'C':
        return 'ambiguousCorrect' if label['group'] == 'ambiguous' else 'controlCorrect'
    if label['scenario'] == 'protectedKeep':
        return 'protectedKeep'
    if label['scenario'] == 'unknownKeep':
        return 'unknownKeep'
    return 'proseKeep'


def delta(label, action):
    gold_correct = label['gold'] == 'C'
    return Counter(count=1, autoChanges=int(action == 'C'), autoRecovered=int(gold_correct and action == 'C'),
                   falseAuto=int(not gold_correct and action == 'C'),
                   preserved=int(not gold_correct and action != 'C'), proposals=int(action == 'S'),
                   correctProposals=int(gold_correct and action == 'S'), falseProposals=int(not gold_correct and action == 'S'),
                   abstained=int(action == 'A'))


def budget(groups):
    required = ['proseKeep', 'protectedKeep', 'unknownKeep', 'ambiguousKeep', 'ambiguousCorrect', 'controlCorrect']
    checks = {name + 'ZeroFalse': (groups[name]['falseAuto'] == 0 if groups[name]['count'] else None)
              for name in ['proseKeep','protectedKeep','unknownKeep','ambiguousKeep']}
    checks.update({name + 'AtLeastHalf': (groups[name]['autoRecovered'] * 2 >= groups[name]['count'] if groups[name]['count'] else None)
                   for name in ['ambiguousCorrect','controlCorrect']})
    status = 'insufficient' if any(not groups[x]['count'] for x in required) else 'pass' if all(checks.values()) else 'fail'
    return {'status': status, 'checks': checks}


def run(root, decisions):
    protocol = (root / 'data/experiment-005-protocol.json').read_bytes()
    if hashlib.sha256(protocol).hexdigest() != SHA:
        raise ValueError('protocol differs')
    manifest = json.loads((root / 'data/evaluation-005-sources.json').read_text())
    raws = {}
    for field in ['features', 'labels']:
        ref = manifest[field];p = root / ref['path']
        if not ref['path'].startswith('build/experiment-005-data/') or p.resolve() != p.absolute():
            raise ValueError('unsafe data path')
        raw = p.read_bytes()
        if len(raw) != ref['bytes'] or hashlib.sha256(raw).hexdigest() != ref['sha256']:
            raise ValueError('data differs')
        raws[field] = raw
    rows = raws['features'].decode().splitlines()
    labels = [json.loads(line) for line in raws['labels'].decode().splitlines()]
    outputs = decisions.read_text().splitlines()
    if not len(rows) == len(labels) == len(outputs) == manifest['features']['rows']:
        raise ValueError('row count differs')
    stats = defaultdict(lambda: defaultdict(lambda: defaultdict(lambda: defaultdict(Counter))))
    seen = set()
    collisions = defaultdict(dict)
    token_groups = defaultdict(set)
    keygroups = defaultdict(set)
    pair_facts = defaultdict(Counter)
    reference_matched = 0
    for i, (row, label, output) in enumerate(zip(rows, labels, outputs)):
        fields = row.split('\t');ordinal, vote, codes = output.split('\t')
        expected_vote, expected_codes = reference(fields)
        if ordinal != str(i) or vote != expected_vote or codes != expected_codes or label['ordinal'] != i:
            raise ValueError('independent policy mismatch at ordinal ' + str(i))
        reference_matched += 1
        category = classify(label)
        signature = (row, label['gold'], label['group'], category)
        fresh = signature not in seen;seen.add(signature)
        split = label['split'];layout = label['layout'];length = 'short2to3' if label['length'] < 4 else 'long4plus'
        keygroups[split].add(label['keyGroup'])
        token_groups[label['keyGroup'], layout].add(codes[1:3])
        for j, method in enumerate(METHODS):
            d = delta(label, codes[j])
            for scope in [split + '/pooled', split + '/' + layout, split + '/' + length]:
                for unit, include in [('occurrences', True), ('types', fresh)]:
                    if not include:
                        continue
                    stats[method][scope][unit][category].update(d)
                    if label['group'] == 'ambiguous' and label['gold'] == 'K':
                        stats[method][scope][unit]['ambiguousKeep'].update(d)
            if label['scenario'] == 'borrowKeep':
                pair_facts[method]['borrowFalseAuto'] += codes[j] == 'C'
            if label['group'] == 'ambiguous' and label['scenario'] == 'switchCorrect':
                pair_facts[method]['ambiguousSwitchAutoRecovered'] += codes[j] == 'C'
        if label['collisionPair']:
            pair = collisions[split, label['collisionPair']]
            if label['gold'] in pair:
                raise ValueError('duplicate collision role')
            pair[label['gold']] = (row, codes)
    if keygroups['development'] & keygroups['holdout']:
        raise ValueError('development/holdout key overlap')
    if any(len(actions) != 1 for actions in token_groups.values()):
        raise ValueError('token baseline depends on context')
    collision_summary = {split: {method: Counter() for method in METHODS} for split in ['development','holdout']}
    for (split, key), pair in collisions.items():
        if set(pair) != {'K','C'} or pair['K'][0] != pair['C'][0] or pair['K'][1] != pair['C'][1]:
            raise ValueError('collision observables/action mismatch')
        for j, method in enumerate(METHODS):
            action = pair['K'][1][j]
            recovered, preserved = int(action == 'C'), int(action != 'C')
            if recovered + preserved > 1:
                raise ValueError('collision ceiling violated')
            collision_summary[split][method].update(pairs=1, correctIntentAutoRecovered=recovered,
                                                   keepIntentPreserved=preserved, jointlySatisfied=int(recovered and preserved))
    report = {'experiment':'SYS-237 experiment005','protocolSha256':SHA,
              'features':{k:manifest['features'][k] for k in ['sha256','bytes','rows']},
              'labelsSha256':manifest['labels']['sha256'], 'selection':manifest['selection_summary'],
              'independentPolicyRowsMatched':reference_matched, 'tokenBaselineContextInvariance':True,
              'keygroupSplitOverlap':0, 'methods':{}, 'collisionPairs':{}, 'contextAutoVsVeto':{},
              'limitations':['synthetic constructed intent, not sampled natural language',
                             'two-script vote not semantic understanding; prefix templates encode ordinary-case language',
                             'target keys disjoint between005 partitions, not necessarily unseen by old dictionary/model training',
                             'new context guard>=2 differs from old token guard>=4; length slices reported separately',
                             'oracle protected/unknown field metadata, not actual keyboard context detection',
                             'identical-observable conflicting intents deliberately nonidentifiable; no population accuracy inferred']}
    categories = ['proseKeep','protectedKeep','unknownKeep','ambiguousKeep','ambiguousCorrect','controlCorrect']
    fields = ['count','autoChanges','autoRecovered','falseAuto','preserved','proposals','correctProposals','falseProposals','abstained']
    for method in METHODS:
        out = report['methods'][method] = {}
        for scope, units in stats[method].items():
            result = out[scope] = {}
            for unit, groups in units.items():
                result[unit] = {}
                for category in categories:
                    c = groups[category]
                    result[unit][category] = {field:c[field] for field in fields}
                    result[unit][category]['autoRecoveryRate'] = c['autoRecovered']/c['count'] if c['count'] and category.endswith('Correct') else None
                result[unit]['budget'] = budget(groups)
        report['contextAutoVsVeto'][method] = dict(pair_facts[method])
    for split, methods in collision_summary.items():
        report['collisionPairs'][split] = {method:dict(c) for method,c in methods.items()}
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decisions', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = Path(args.output).absolute()
    if output.exists() or output.resolve() != output or not output.is_relative_to(root/'build'):
        raise ValueError('output must be new inside build')
    report = run(root, Path(args.decisions))
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print('All policies/collisions matched; aggregate report written without text.')
