#!/usr/bin/env python3
"""Independent list/filter recount of experiment005 aggregates and budget checks."""
import argparse
import json
from pathlib import Path

NAMES=['alwaysKeep','tokenTwoScore','tokenHunspell','contextAuto','guardedContextAuto','contextSuggest']


def category(label):
    if label['gold']=='C':return 'ambiguousCorrect' if label['group']=='ambiguous' else 'controlCorrect'
    return {'protectedKeep':'protectedKeep','unknownKeep':'unknownKeep'}.get(label['scenario'],'proseKeep')


def check(root, report, output):
    manifest=json.loads((root/'data/evaluation-005-sources.json').read_text())
    labels=[json.loads(line) for line in (root/manifest['labels']['path']).read_text().splitlines()]
    features=(root/manifest['features']['path']).read_text().splitlines()
    codes=[line.split('\t')[2] for line in output.read_text().splitlines()]
    checked=verdicts=0
    for method_index,method in enumerate(NAMES):
        for scope,result in report['methods'][method].items():
            split,part=scope.split('/')
            selected=[i for i,label in enumerate(labels) if label['split']==split and (
                part=='pooled' or part==label['layout'] or part==('short2to3' if label['length']<4 else 'long4plus'))]
            for unit,groups in result.items():
                indices=selected
                if unit=='types':
                    first={}
                    for i in selected:
                        first.setdefault((features[i],labels[i]['gold'],labels[i]['group'],category(labels[i])),i)
                    indices=list(first.values())
                independent={}
                for group in ['proseKeep','protectedKeep','unknownKeep','ambiguousKeep','ambiguousCorrect','controlCorrect']:
                    ids=([i for i in indices if labels[i]['group']=='ambiguous' and labels[i]['gold']=='K']
                         if group=='ambiguousKeep' else [i for i in indices if category(labels[i])==group])
                    actual={'count':len(ids),'autoChanges':sum(codes[i][method_index]=='C' for i in ids),
                            'autoRecovered':sum(labels[i]['gold']=='C' and codes[i][method_index]=='C' for i in ids),
                            'falseAuto':sum(labels[i]['gold']=='K' and codes[i][method_index]=='C' for i in ids),
                            'proposals':sum(codes[i][method_index]=='S' for i in ids),
                            'correctProposals':sum(labels[i]['gold']=='C' and codes[i][method_index]=='S' for i in ids),
                            'falseProposals':sum(labels[i]['gold']=='K' and codes[i][method_index]=='S' for i in ids)}
                    for key,value in actual.items():
                        if groups[group][key]!=value:raise ValueError('independent aggregate mismatch')
                    independent[group]=actual;checked+=len(actual)
                empty=any(not value['count'] for value in independent.values())
                false=sum(independent[group]['falseAuto'] for group in ['proseKeep','protectedKeep','unknownKeep','ambiguousKeep'])
                enough=all(independent[group]['autoRecovered']/independent[group]['count']>=0.5 for group in ['ambiguousCorrect','controlCorrect'] if independent[group]['count'])
                verdict='insufficient' if empty else 'fail' if false or not enough else 'pass'
                if verdict!=groups['budget']['status']:raise ValueError('independent budget mismatch')
                verdicts+=1
    print(json.dumps({'primaryCounterFieldsMatched':checked,'budgetVerdictsMatched':verdicts}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',required=True);parser.add_argument('--decisions',required=True)
    a=parser.parse_args();check(Path(__file__).resolve().parent.parent,json.loads(Path(a.report).read_text()),Path(a.decisions))
