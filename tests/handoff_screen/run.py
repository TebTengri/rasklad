#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fixed symbolic contract screen; aggregate output, no real input/transport."""
import argparse
import json
from model import run_cases

def run():
    expected={
        'same_target_race_pair':('active',1,1),
        'stale_ack':('active',1,1),
        'persistent_rebind':('active',1,1),
        'target_switch_queued_press':('unresolved_unsupported_queued_target_switch',0,0),
        'held_key_target_switch':('unresolved_unsupported_held_key_target_switch',1,0),
        'held_grab_transfer':('unresolved_unsupported_held_state_transfer',1,0),
        'unmatched_release':('rejected_duplicate_or_unmatched_input',0,0),
        'duplicate_release':('rejected_duplicate_or_unmatched_input',1,1),
        'overflow':('rejected_queue_overflow',0,0),
    }
    cases=run_cases()
    if set(cases)!=set(expected):raise ValueError('case_inventory')
    rows=[]
    for name,case in cases.items():
        press=sum(state=='press' for _,_,state in case['trace'])
        release=sum(state=='release' for _,_,state in case['trace'])
        if (case['outcome'],press,release)!=expected[name]:raise ValueError('unexpected_screen_outcome')
        rows.append({'case':name,'outcome':case['outcome'],'deliveredPresses':press,
                     'deliveredReleases':release,'acceptedDelivery':case['outcome']=='active'})
    return {'schema':1,'scope':'pure cooperative symbolic contract screen; no protocol compatibility',
            'expectedScreenOutcomesMatched':True,'acceptedDeliveryCases':3,'explicitRejectionCases':6,
            'functionalFix':False,'realWire':False,'fcitxStarted':False,'compositorStarted':False,
            'cases':rows,'missing':['timeouts','target incarnation reuse','keymap/modifier coherence',
                                  'repeat','disconnect/shutdown','real protocol binding']}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run',action='store_true');args=parser.parse_args()
    try:print(json.dumps(run() if args.run else {'status':'plan_only','functionalFix':False},sort_keys=True))
    except Exception as e:print(json.dumps({'screenFailed':True,'reason':type(e).__name__}));raise SystemExit(2)
