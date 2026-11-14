// SPDX-License-Identifier: MIT
// Fixed-counter contract and integration faults; not a real D-Bus transport proof.
import test from 'node:test';
import assert from 'node:assert/strict';
import {parseWitnessReply,createAddonWitness,WITNESS_COUNTERS} from '../chromium_ime/addon-witness.mjs';
import {runFocusScenarios} from '../chromium_ime/adversarial-focus.mjs';
import {fixture} from './focus_fixture.mjs';
const reply=(values=Array(20).fill(0),schema=1,enabled=1)=>`([uint32 ${[schema,enabled,...values].join(', ')}],)\n`;
test('fixed reply accepts only exact schema enabled marker and bounded20 counters',()=>{
 assert.equal(WITNESS_COUNTERS.length,20);assert.deepEqual(parseWitnessReply(reply()),Array(20).fill(0));
 for(const bad of [reply([],1,1),reply(Array(20).fill(0),2,1),reply(Array(20).fill(0),1,0),
  reply(Array(20).fill(256)),reply(Array(20).fill(-1)),'PRIVATE_REPLY',reply()+'PRIVATE', 'x'.repeat(769)])assert.equal(parseWitnessReply(bad),null);
});
test('valid samples are one shot with exact deltas and no external state changes',async()=>{
 let calls=0;const p=createAddonWitness({query:async()=>reply(Array(20).fill(calls++))});
 assert.equal(await p.begin(),true);assert.equal(await p.begin(),false);
 const r=await p.sample();assert.equal(r.valid,true);assert.equal(r.complete,true);assert.deepEqual(r.delta,Array(20).fill(1));
 assert.equal((await p.sample()).valid,false);assert.equal(calls,2);
});
test('saturation is incomplete with unknown delta rather than zero',async()=>{
 let calls=0;const p=createAddonWitness({query:async()=>reply(Array(20).fill(calls++?255:0))});
 await p.begin();const r=await p.sample();assert.equal(r.valid,true);assert.equal(r.complete,false);assert.deepEqual(r.delta,Array(20).fill(-1));
});
test('counter reset or missing endpoint is invalid and never exports raw output',async()=>{
 let calls=0;const p=createAddonWitness({query:async()=>reply(Array(20).fill(calls++?0:1))});
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
  return reply(Array(20).fill(before?0:1));
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
