// SPDX-License-Identifier: MIT
// Fixed-counter contract and integration faults; not a real D-Bus transport proof.
import test from 'node:test';
import assert from 'node:assert/strict';
import {parseWitnessReply,createAddonWitness,WITNESS_COUNTERS,CORE_ENTRY_COUNTERS} from '../chromium_ime/addon-witness.mjs';
import {runFocusScenarios} from '../chromium_ime/adversarial-focus.mjs';
import {fixture} from './focus_fixture.mjs';
const reply=(values=Array(22).fill(0),schema=2,enabled=1)=>`([uint32 ${[schema,enabled,...values].join(', ')}],)\n`;
test('fixed reply accepts only exact schema enabled marker and bounded22 counters',()=>{
 assert.equal(WITNESS_COUNTERS.length,22);assert.deepEqual(parseWitnessReply(reply()),Array(22).fill(0));
 for(const bad of [reply([],1,1),reply(Array(22).fill(0),1,1),reply(Array(22).fill(0),2,0),
  reply(Array(20).fill(0),1,1),reply(Array(22).fill(256)),reply(Array(22).fill(-1)),'PRIVATE_REPLY',reply()+'PRIVATE', 'x'.repeat(769)])assert.equal(parseWitnessReply(bad),null);
});
test('valid samples are one shot with exact deltas and no external state changes',async()=>{
 let calls=0;const p=createAddonWitness({query:async()=>reply(Array(22).fill(calls++))});
 assert.equal(await p.begin(),true);assert.equal(await p.begin(),false);
 const r=await p.sample();assert.equal(r.valid,true);assert.equal(r.complete,true);assert.deepEqual(r.delta,Array(22).fill(1));
 assert.equal((await p.sample()).valid,false);assert.equal(calls,2);
});
test('saturation is incomplete with unknown delta rather than zero',async()=>{
 let calls=0;const p=createAddonWitness({query:async()=>reply(Array(22).fill(calls++?255:0))});
 await p.begin();const r=await p.sample();assert.equal(r.valid,true);assert.equal(r.complete,false);assert.deepEqual(r.delta,Array(22).fill(-1));
});
test('counter reset or missing endpoint is invalid and never exports raw output',async()=>{
 let calls=0;const p=createAddonWitness({query:async()=>reply(Array(22).fill(calls++?0:1))});
 await p.begin();assert.equal((await p.sample()).valid,false);
 const q=createAddonWitness({query:async()=>{throw Error('PRIVATE_ERROR');}});
 assert.equal(await q.begin(),false);assert.equal(JSON.stringify(await q.sample()).includes('PRIVATE'),false);
});
test('only case2 is sampled before word and after oracle, with separate post-query snapshot',async()=>{
 const f=fixture();let calls=0;
 const witness=createAddonWitness({query:async()=>{
  const before=calls++===0;
  assert.equal(await f.ev(`window.__raskladFocus.token`),'word_other_back_prose_space');
  assert.equal(await f.ev(`document.getElementById('prose').value`),before?'':'ghbdtn ');
  return reply(Array(22).fill(before?0:1));
 }});
 const r=await runFocusScenarios({...f,addonWitness:witness});assert.equal(r.passed,true);assert.equal(calls,2);
 assert.equal(r.cases[1].addonWitness.valid,true);assert.equal(r.cases[1].witnessPostQuery.snapshotCode,100);
 assert.ok(r.cases.filter((_,i)=>i!==1).every(c=>c.addonWitness===undefined));assert.equal(f.timers.size,0);
});
test('late change during counter query cannot rescue original failure',async()=>{
 const f=fixture('word_lost');let calls=0;
 const witness=createAddonWitness({query:async()=>{
  if(calls++===1)await f.ev(`document.getElementById('prose').value='ghbdtn '`);
  return reply();
 }});
 const r=await runFocusScenarios({...f,addonWitness:witness});const c=r.cases[1];
 assert.equal(c.passed,false);assert.equal(c.errorCode,'ASSERTION_FAILED');
 assert.equal(c.witnessPostQuery.matchesOriginalOracle,true);assert.equal(c.witnessPostQuery.snapshotCode,100);
});
for(const failAt of [1,2])test(`query${failAt} failure cannot pass and gates dependent cases`,async()=>{
 const f=fixture();let calls=0;const witness=createAddonWitness({query:async()=>++calls===failAt?'PRIVATE_PAYLOAD':reply()});
 const r=await runFocusScenarios({...f,addonWitness:witness});assert.equal(r.passed,false);
 assert.equal(r.cases[1].passed,false);assert.equal(r.cases[1].cleanupPassed,false);
 assert.ok(r.cases.slice(2).every(c=>c.errorCode==='DEPENDENT_NOT_RUN'));
 assert.equal(JSON.stringify(r).includes('PRIVATE_PAYLOAD'),false);assert.equal(f.timers.size,0);
});

test('no-query control never invokes query and uses unchanged strict oracle path',async()=>{
 const forbidden={noQuery:true};Object.defineProperty(forbidden,'query',{get(){throw Error('query getter must not run');}});
 assert.equal(createAddonWitness(forbidden),null);
 let calls=0;const witness=createAddonWitness({query:async()=>{calls++;throw Error('MUST_NOT_RUN');},noQuery:true});
 assert.equal(witness,null);const f=fixture();const r=await runFocusScenarios({...f,addonWitness:witness});
 assert.equal(r.passed,true);assert.equal(calls,0);assert.ok(r.cases.every(c=>c.addonWitness===undefined));
});

const coreReply=(own=Array(22).fill(0),core=Array(10).fill(0),schema=3,shimSchema=1,ready=1)=>
 `([uint32 ${[schema,1,...own,shimSchema,ready,...core].join(', ')}],)`;
test('core entry requires explicit mode, exact schema and loaded ready shim',()=>{
 assert.equal(CORE_ENTRY_COUNTERS.length,10);
 assert.equal(parseWitnessReply(coreReply()),null);
 assert.equal(parseWitnessReply(reply(),true),null);
 assert.deepEqual(parseWitnessReply(coreReply(),true),Array(32).fill(0));
 for(const bad of [coreReply(undefined,undefined,2),coreReply(undefined,undefined,3,0),
  coreReply(undefined,undefined,3,1,0),coreReply(Array(22).fill(0),Array(10).fill(256)),
  coreReply(Array(22).fill(0),Array(9).fill(0)),coreReply(Array(22).fill(-1))])
  assert.equal(parseWitnessReply(bad,true),null);
});
test('core entry deltas preserve own then core counter order and saturation',async()=>{
 let calls=0;
 const p=createAddonWitness({coreEntry:true,query:async()=>{
  const after=calls++!==0;
  return coreReply(Array.from({length:22},(_,i)=>after?i+2:1),
   Array.from({length:10},(_,i)=>after?i+31:1));
 }});
 assert.equal(await p.begin(),true);const r=await p.sample();
 assert.equal(r.valid,true);assert.equal(r.complete,true);
 assert.deepEqual(r.delta,[...Array.from({length:22},(_,i)=>i+1),...Array.from({length:10},(_,i)=>i+30)]);
 let n=0;const q=createAddonWitness({coreEntry:true,query:async()=>coreReply(Array(22).fill(1),Array(10).fill(n++?255:1))});
 await q.begin();const saturated=await q.sample();assert.equal(saturated.complete,false);
 assert.deepEqual(saturated.delta.slice(22),Array(10).fill(-1));
});

test('core mode requires prior own and core press calibration, not ready flag alone',async()=>{
 for(const [ownPress,corePress] of [[0,1],[1,0],[0,0]]){
  let calls=0;const own=Array(22).fill(1),core=Array(10).fill(1);
  own[20]=ownPress;core[0]=corePress;
  const p=createAddonWitness({coreEntry:true,query:async()=>{calls++;return coreReply(own,core);}});
  assert.equal(await p.begin(),false);assert.equal((await p.sample()).valid,false);
  assert.equal(calls,1);assert.equal(await p.begin(),false);
 }
 const own=Array(22).fill(1),core=Array(10).fill(1);own[20]=2;core[0]=9;
 const p=createAddonWitness({coreEntry:true,query:async()=>coreReply(own,core)});
 assert.equal(await p.begin(),true);assert.equal((await p.sample()).valid,true);
});
