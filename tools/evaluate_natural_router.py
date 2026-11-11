#!/usr/bin/env python3
"""Independent fixed-router audit, contextual/word-type metrics, architecture verdict.

All labels are read here AFTER C++ policy outputs. No fitting or source retrieval.
Word-type errors use ANY event across contexts, not an arbitrary first occurrence.
"""
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import argparse

SHA='0dbd4b61d265293a071a01fc74e047d968fd3dc264d2b0d0b3c37811a703560b'
NAMES=['alwaysKeep','safeFull','safeConfirmed','contextConfirmed','hybrid','contextHybrid','proposalsOnly','scriptContext']
RU=set('абвгдеёжзийклмнопрстуфхцчшщъыьэюя');RUALL=RU|{x.upper() for x in RU}
US=set("`qwertyuiop[]asdfghjkl;'zxcvbnm,.~QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>")
FIELDS=['count','eligible','C','S','K','A','falseAuto','falseSuggest','eligibleFalseAuto','eligibleFalseSuggest',
        'ordinaryEligible','ordinaryRecovered','ordinarySuggested','ambiguousRecovered','correctSuggest']

FIELD_BITS={name:1<<i for i,name in enumerate(FIELDS)}

def anchor(text):
    if not 4<=len(text)<=32:return '-'
    if all('a'<=c<='z' or 'A'<=c<='Z' for c in text):return 'U'
    if set(text)<=RUALL:return 'R'
    return '-'


def reference(f):
    layout,target,a,b,left,right,field,obs,alt,shared,model,full=f
    v1,v2=anchor(left),anchor(right);vote=v1 if v1==v2 else '-'
    supported=1<=len(target)<=32 and set(target)<=(US if layout=='us' else RUALL)
    eligible=supported and len(target)>=4;opposite=vote!='-' and vote!=('U' if layout=='us' else 'R')
    # Frozen005 legacy rule, intentionally guard>=2 and no shared veto.
    if field=='protected':legacy='K'
    elif field=='unknown' or not supported or len(target)<2 or vote=='-':legacy='A'
    elif obs=='1' and alt=='0':legacy='K'
    elif alt=='1':legacy='K' if not opposite and obs=='1' else 'C' if opposite else 'A'
    else:legacy='A'
    if field=='protected':new=['K']*6
    elif field=='unknown' or not eligible:new=['A']*6
    elif obs=='1':new=['K']*6
    elif shared=='1':new=['A']*6
    else:
        safe='C' if alt=='1' else 'A'
        confirm='C' if alt=='1' and model=='C' else 'A'
        context=confirm if opposite else 'A'
        hybrid=('C' if model=='C' else 'S') if alt=='1' else ('S' if model=='C' else 'A')
        filtered=hybrid if alt=='1' or hybrid!='S' or opposite else 'A'
        proposal='S' if filtered=='C' else filtered
        new=[safe,confirm,context,hybrid,filtered,proposal]
    return vote,'K'+''.join(new)+legacy,eligible


def fact(label,action,eligible):
    correct=label['gold']=='C';shared=label['shared']
    values={'count':1,'eligible':int(eligible),action:1,'falseAuto':int(not correct and action=='C'),
            'falseSuggest':int(not correct and action=='S'),'eligibleFalseAuto':int(not correct and eligible and action=='C'),
            'eligibleFalseSuggest':int(not correct and eligible and action=='S'),
            'ordinaryEligible':int(correct and eligible and not shared),
            'ordinaryRecovered':int(correct and eligible and not shared and action=='C'),
            'ordinarySuggested':int(correct and eligible and not shared and action=='S'),
            'ambiguousRecovered':int(correct and shared and action=='C'),'correctSuggest':int(correct and action=='S')}
    return Counter({key:value for key,value in values.items() if value})


def event_mask(label,action,eligible):
    return sum(FIELD_BITS[key] for key in fact(label,action,eligible))


def metric(c):
    out={key:int(c[key]) for key in FIELDS}
    out['ordinaryRecoveryRate']=c['ordinaryRecovered']/c['ordinaryEligible'] if c['ordinaryEligible'] else None
    out['falseAutoPer10000Eligible']=10000*c['eligibleFalseAuto']/c['eligible'] if c['eligible'] else None
    out['falseSuggestPer10000Eligible']=10000*c['eligibleFalseSuggest']/c['eligible'] if c['eligible'] else None
    return out


def screen(units,kind):
    checks={}
    for unit in ['occurrences','wordTypes']:
        x=units[unit];v=x['valid'];n=v['eligible'];false=v['eligibleFalseAuto' if kind=='auto' else 'eligibleFalseSuggest']
        checks[unit+'ValidBudget']=false*10000<=n if n else None
        for role in ['protected','unknown']:
            checks[unit+role+'Zero']=x[role]['falseAuto' if kind=='auto' else 'falseSuggest']==0 if x[role]['count'] else None
        if kind=='auto':
            checks[unit+'AmbiguousKeepZero']=x['ambiguousValid']['falseAuto']==0
            for role in ['single','burst']:
                c=x[role];checks[unit+role+'Recovery']=c['ordinaryRecovered']*2>=c['ordinaryEligible'] if c['ordinaryEligible'] else None
    if kind=='proposal':checks['AddsUsefulProposal']=sum(units['occurrences'][k]['correctSuggest'] for k in ['single','burst'])>0
    status='insufficient' if any(v is None for v in checks.values()) else 'pass' if all(checks.values()) else 'fail'
    return {'status':status,'checks':checks}


def load_verified(root,ref):
    path=root/ref['path']
    if path.resolve()!=path.absolute() or not ref['path'].startswith('build/experiment-006-data/'):raise ValueError('input path')
    raw=path.read_bytes()
    if len(raw)!=ref['bytes'] or hashlib.sha256(raw).hexdigest()!=ref['sha256']:raise ValueError('input hash')
    return raw


def evaluate(root):
    if hashlib.sha256((root/'data/experiment-006-protocol.json').read_bytes()).hexdigest()!=SHA:raise ValueError('protocol')
    manifest=json.loads((root/'data/evaluation-006-sources.json').read_text())
    counters=defaultdict(Counter);wordstates=defaultdict(int);seen=set();rows=0;source_docs={}
    for d in manifest['documents']:source_docs[d['index']]=d
    for shard,(fr,lr) in enumerate(zip(manifest['features'],manifest['labels'])):
        features=load_verified(root,fr).decode().splitlines();labels=[json.loads(s) for s in load_verified(root,lr).decode().splitlines()]
        outputs=(root/f'build/experiment-006-data/actions-{shard:02d}.tsv').read_text().splitlines()
        if not len(features)==len(labels)==len(outputs)==fr['rows']:raise ValueError('shard rows')
        for local,(feature,label,output) in enumerate(zip(features,labels,outputs)):
            f=feature.split('\t');ordinal,vote,codes=output.split('\t');rv,rc,eligible=reference(f)
            if ordinal!=str(local) or vote!=rv or codes!=rc or label['ordinal']!=rows or label['eligible']!=eligible:raise ValueError('independent router mismatch at ordinal '+str(rows))
            if label['shard']!=shard or label['row']!=local or label['shared']!=(f[9]=='1'):raise ValueError('label metadata')
            split,lang,kind=label['split'],label['language'],label['kind'];length='long4plus' if eligible else 'shortOrUnsupported'
            scopes=[split+'/pooled',split+'/'+lang,split+'/'+length]
            if label['borrowedEnglish'] and kind=='valid':extra=['borrowedValid']
            else:extra=[]
            if label['shared'] and kind=='valid':extra+=['ambiguousValid']
            roles=[kind]+extra
            sig=(split,lang,kind,label['gold'],feature);fresh=sig not in seen;seen.add(sig)
            for method_index,name in enumerate(NAMES):
                delta=fact(label,codes[method_index],eligible)
                mask=sum(FIELD_BITS[key] for key in delta)
                for scope in scopes:
                    for role in roles:
                        counters[name,scope,'occurrences',role].update(delta)
                        if fresh:counters[name,scope,'inputTypes',role].update(delta)
                        key=(name,scope,role,lang,kind,label['layout'],label['wordHash'])
                        wordstates[key] |= mask
            rows+=1
    for key,mask in wordstates.items():
        name,scope,role,*word=key
        counters[name,scope,'wordTypes',role].update({field:1 for field,bit in FIELD_BITS.items() if mask & bit})
    report={'experiment':'SYS-238 experiment006','protocolSha256':SHA,'independentRowsMatched':rows,'selection':manifest['preparation_summary'],
            'methods':{},'architecture':{},'limitations':['same translated technical domain; no natural chat or user typing',
            'single/burst synthetic wrong copies, not deployment prevalence; unknown no-action stress is normative',
            'oracle field/layout and source-level Markdown; no actual safe desktop context implemented',
            'wordTypes use any event over contexts; context input types differ from target-word types',
            'correlated words/documents; empirical screen is not production confidence']}
    scopes=sorted({k[1] for k in counters});roles=['valid','protected','unknown','single','burst','borrowedValid','ambiguousValid']
    for name in NAMES:
        dest=report['methods'][name]={}
        for scope in scopes:
            units=dest[scope]={unit:{role:metric(counters[name,scope,unit,role]) for role in roles} for unit in ['occurrences','inputTypes','wordTypes']}
            dest[scope]['autoScreen']=screen(units,'auto');dest[scope]['proposalScreen']=screen(units,'proposal')
    architecture(report)
    return report


def architecture(report):
    m=report['methods'];scopes=['holdout/en','holdout/ru','holdout/pooled'];units=['occurrences','wordTypes'];kinds=['single','burst']
    confirmed_pass=all(m['safeConfirmed'][s]['autoScreen']['status']=='pass' for s in scopes)
    penalties=[]
    for scope in scopes:
        for unit in units:
            for kind in kinds:
                base=m['safeFull'][scope][unit][kind]['ordinaryRecoveryRate'];candidate=m['safeConfirmed'][scope][unit][kind]['ordinaryRecoveryRate']
                penalties.append(None if base is None or candidate is None else base-candidate)
    penalty_ok=all(p is not None and p<=0.10+1e-12 for p in penalties)
    prevent=sum(m['safeConfirmed'][s]['occurrences']['valid']['falseAuto']-m['contextConfirmed'][s]['occurrences']['valid']['falseAuto'] for s in ['holdout/en','holdout/ru'])
    context_losses=[]
    for scope in scopes:
        for unit in units:
            for kind in kinds:
                a=m['safeConfirmed'][scope][unit][kind]['ordinaryRecoveryRate'];b=m['contextConfirmed'][scope][unit][kind]['ordinaryRecoveryRate'];context_losses.append(None if a is None or b is None else a-b)
    context_required=prevent>0 and all(v is not None and v<=0.05+1e-12 for v in context_losses)
    proposal_pass=all(m['contextHybrid'][s]['proposalScreen']['status']=='pass' for s in scopes)
    false_prevented=m['hybrid']['holdout/pooled']['occurrences']['valid']['falseSuggest']-m['contextHybrid']['holdout/pooled']['occurrences']['valid']['falseSuggest']
    good=lambda name:sum(m[name]['holdout/pooled']['occurrences'][k]['correctSuggest'] for k in kinds)
    retained=good('contextHybrid')/good('hybrid') if good('hybrid') else None
    report['architecture']={'algorithmCandidate':'safeConfirmed' if confirmed_pass and penalty_ok else 'manualOnly',
                            'safeConfirmedAllAutoScreensPass':confirmed_pass,'confirmedRecallPenaltyWithin10pp':penalty_ok,
                            'maxConfirmedPenaltyPp':100*max(p for p in penalties if p is not None) if any(p is not None for p in penalties) else None,
                            'mandatoryContext':context_required,'contextPreventedNaturalFalseAuto':prevent,
                            'maxMandatoryContextRecoveryLossPp':100*max(p for p in context_losses if p is not None) if any(p is not None for p in context_losses) else None,
                            'proactiveSuggestions':proposal_pass,'contextHybridAllProposalScreensPass':proposal_pass,
                            'contextFilterCredited':false_prevented>0 and retained is not None and retained>=0.5,
                            'contextFilterFalseSuggestPrevented':false_prevented,'contextFilterCorrectSuggestRetention':retained,
                            'desktopDefault':'explicit manual selection/preview/undo until independently verified safe input context; never auto-enable from this report'}


if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--output',required=True);args=a.parse_args();root=Path(__file__).resolve().parent.parent
    output=Path(args.output).absolute()
    if output.exists() or output.resolve()!=output or not output.is_relative_to(root/'build'):raise ValueError('new output only inside build')
    p=evaluate(root);output.write_text(json.dumps(p,ensure_ascii=False,indent=2)+'\n');print('All router decisions matched; metrics and preregistered architecture decision written.')
