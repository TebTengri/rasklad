#!/usr/bin/env python3
"""Compare primary report counts and budget verdicts with independent audit facts."""
import argparse
import json
from pathlib import Path

NAMES = ['alwaysKeep', 'twoScore', 'hunspellFull', 'modelConfirmedMorph', 'morphThenModel',
         'protectedTwoScore', 'protectedHunspellFull', 'protectedConfirmedMorph', 'protectedMorphThenModel']
FIELDS = {'validEligible': ('valid', 'eligible'), 'validFalse': ('valid', 'changes'),
          'validEligibleFalse': ('valid', 'eligibleChanges'), 'codeFalse': ('protected', 'changes'),
          'wrongEligible': ('wrong', 'eligible'), 'recovered': ('wrong', 'recovered'),
          'eligibleRecovered': ('wrong', 'eligibleRecovered'), 'ambiguousUnsafe': ('ambiguousWrong', 'unsafe')}


def check(report, audit):
    for key, value in audit['primaryFacts'].items():
        method, scope, unit, field = key.split('|')
        role, metric = FIELDS[field]
        if report['methods'][NAMES[int(method)]][scope][unit][role][metric] != value:
            raise ValueError('primary aggregate mismatch')
    for name in NAMES:
        for scope in ['en', 'ru', 'pooled']:
            r = report['methods'][name][scope]
            occ, typ = r['occurrences'], r['types']
            n, t, w = occ['valid']['eligible'], typ['valid']['eligible'], occ['wrong']['eligible']
            if not n or not t or not w:
                expected = 'insufficient'
            else:
                passed = (occ['valid']['eligibleChanges'] / n <= 0.0001
                          and typ['valid']['eligibleChanges'] / t <= 0.0001
                          and not occ['protected']['changes']
                          and occ['wrong']['eligibleRecovered'] / w >= 0.5)
                expected = 'pass' if passed else 'fail'
            if expected != r['screen']['status']:
                raise ValueError('budget verdict mismatch')
    print('Independent primary counts and all 27 language/pooled budget verdicts matched.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', required=True)
    parser.add_argument('--audit', required=True)
    args = parser.parse_args()
    check(json.loads(Path(args.report).read_text()), json.loads(Path(args.audit).read_text()))
