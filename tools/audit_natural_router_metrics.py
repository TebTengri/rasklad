#!/usr/bin/env python3
"""Separate primary recount with per-event word sets, not evaluator bit masks."""
from collections import Counter,defaultdict
import argparse,json
from pathlib import Path

NAMES=['alwaysKeep','safeFull','safeConfirmed','contextConfirmed','hybrid','contextHybrid','proposalsOnly','scriptContext']
FIELDS=['count','eligible','falseAuto','falseSuggest','eligibleFalseAuto','eligibleFalseSuggest','ordinaryEligible','ordinaryRecovered','ordinarySuggested','ambiguousRecovered','correctSuggest']


def check(root,report):
    m=json.loads((root/'data/evaluation-006-sources.json').read_text());counts=Counter();words=defaultdict(set)
    for shard,lr in enumerate(m['labels']):
        labels=[json.loads(x) for x in (root/lr['path']).read_text().splitlines()]
        actions=[x.split('\t')[2] for x in (root/f'build/experiment-006-data/actions-{shard:02d}.tsv').read_text().splitlines()]
        for label,codes in zip(labels,actions):
            kinds=[label['kind']]
            if label['kind']=='valid' and label['shared']:kinds.append('ambiguousValid')
            if label['kind']=='valid' and label['borrowedEnglish']:kinds.append('borrowedValid')
            scopes=[label['split']+'/pooled',label['split']+'/'+label['language']]
            word=(label['language'],label['layout'],label['wordHash'])
            eligible=label['eligible'];correct=label['gold']=='C';ordinary=correct and eligible and not label['shared']
            for j,name in enumerate(NAMES):
                action=codes[j]
                events={'count':True,'eligible':eligible,'falseAuto':not correct and action=='C','falseSuggest':not correct and action=='S',
                        'eligibleFalseAuto':not correct and eligible and action=='C','eligibleFalseSuggest':not correct and eligible and action=='S',
                        'ordinaryEligible':ordinary,'ordinaryRecovered':ordinary and action=='C','ordinarySuggested':ordinary and action=='S',
                        'ambiguousRecovered':correct and label['shared'] and action=='C','correctSuggest':correct and action=='S'}
                for scope in scopes:
                    for kind in kinds:
                        for field,active in events.items():
                            if active:counts[name,scope,kind,field]+=1;words[name,scope,kind,field].add(word)
    checked=verdicts=0
    for name in NAMES:
        for scope in ['development/en','development/ru','development/pooled','holdout/en','holdout/ru','holdout/pooled']:
            result=report['methods'][name][scope]
            for unit in ['occurrences','wordTypes']:
                for kind in ['valid','protected','unknown','single','burst','ambiguousValid','borrowedValid']:
                    for field in FIELDS:
                        key=(name,scope,kind,field);n=counts[key] if unit=='occurrences' else len(words[key])
                        if result[unit][kind][field]!=n:raise ValueError('independent recount mismatch')
                        checked+=1
            for screen_name in ['autoScreen','proposalScreen']:
                auto=screen_name=='autoScreen';checks=[]
                for unit in ['occurrences','wordTypes']:
                    v=result[unit]['valid'];n=v['eligible'];f=v['eligibleFalseAuto' if auto else 'eligibleFalseSuggest']
                    checks.append(None if not n else f/n<=0.0001)
                    for kind in ['protected','unknown']:
                        x=result[unit][kind];checks.append(None if not x['count'] else x['falseAuto' if auto else 'falseSuggest']==0)
                    if auto:
                        checks.append(result[unit]['ambiguousValid']['falseAuto']==0)
                        for kind in ['single','burst']:
                            x=result[unit][kind];checks.append(None if not x['ordinaryEligible'] else x['ordinaryRecovered']/x['ordinaryEligible']>=0.5)
                if not auto:checks.append(sum(result['occurrences'][k]['correctSuggest'] for k in ['single','burst'])>0)
                verdict='insufficient' if None in checks else 'pass' if all(checks) else 'fail'
                if result[screen_name]['status']!=verdict:raise ValueError('independent budget mismatch')
                verdicts+=1
    print(json.dumps({'primaryCounterFieldsMatched':checked,'primaryScreenVerdictsMatched':verdicts}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--report',required=True);a=p.parse_args()
    check(Path(__file__).resolve().parent.parent,json.loads(Path(a.report).read_text()))
