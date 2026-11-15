// SPDX-License-Identifier: MIT
// Mock transport/phase tests, not compositor-delivery proof.
import test from 'node:test';import assert from 'node:assert/strict';
import {parseServerSnapshots,createServerRouteWitness} from '../chromium_ime/server-route-witness.mjs';
import {runFocusScenarios} from '../chromium_ime/adversarial-focus.mjs';
import {fixture} from './focus_fixture.mjs';
const pair=(patch={})=>{const v=[0x52535731,1,2,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,0];
 for(const [i,n]of Object.entries(patch))v[Number(i)]=n;
 const b=Buffer.alloc(80);v.forEach((n,i)=>b.writeUInt32LE(n,i*4));return[b,Buffer.from(b)];};
test('exact stable schema rejects torn, odd, missing coverage and unknown states',()=>{
 assert.equal(parseServerSnapshots(pair()).length,11);
 for(const p of [{0:0},{1:2},{2:3},{3:0},{4:0},{5:2},{6:65535},{12:1},{17:0},{18:1},{19:1}])assert.equal(parseServerSnapshots(pair(p)),null);
 const torn=pair();torn[1][24]++;assert.equal(parseServerSnapshots(torn),null);
 assert.equal(parseServerSnapshots([Buffer.alloc(79),Buffer.alloc(79)]),null);
});
test('requires calibration and rejects resets, saturation and read failures',async()=>{
 for(const index of [6,8,10]){const p=createServerRouteWitness({readSnapshots:()=>pair({[index]:0})});
  assert.equal(await p.begin(),false);assert.equal((await p.sample()).valid,false);}
 for(const after of [pair({6:0}),pair({6:65535}),pair({12:1})]){
  let n=0;const p=createServerRouteWitness({readSnapshots:()=>n++?after:pair()});
  assert.equal(await p.begin(),true);assert.equal((await p.sample()).valid,false);}
 const p=createServerRouteWitness({readSnapshots:()=>{throw Error('PRIVATE');}});
 assert.equal(await p.begin(),false);assert.equal(JSON.stringify(await p.sample()).includes('PRIVATE'),false);
});
test('fresh repeated readers sample only case2, bounded data and cleanup',async()=>{
 let instances=0,calls=0;const r=await runFocusScenarios({...fixture(),repeatCount:5,serverWitnessFactory:()=>{
  instances++;let n=0;return createServerRouteWitness({readSnapshots:()=>{calls++;return pair({6:1+n++});}});
 }});
 assert.equal(r.passed,true);assert.equal(instances,5);assert.equal(calls,10);
 assert.ok(r.cases.filter(c=>c.iteration).every(c=>c.serverRouteWitness.delta[0]===1&&c.cleanupPassed));
 assert.equal(JSON.stringify(r).includes('1381193521'),false);
});
test('late read cannot rescue failure; invalid sample gates dependents',async()=>{
 const f=fixture('word_lost');let n=0;
 const r=await runFocusScenarios({...f,serverWitnessFactory:()=>createServerRouteWitness({readSnapshots:async()=>{
  if(n++===1)await f.ev("document.getElementById('prose').value='ghbdtn '");return pair();
 }})});
 assert.equal(r.cases[1].passed,false);assert.equal(r.cases[1].serverPostQuery.matchesOriginalOracle,true);
 let calls=0;const bad=await runFocusScenarios({...fixture(),repeatCount:3,serverWitnessFactory:()=>createServerRouteWitness({readSnapshots:()=>calls++?pair({4:0}):pair()})});
 assert.equal(bad.cases[1].dataUnusable,true);assert.equal(bad.cases[1].passed,false);
 assert.ok(bad.cases.slice(2).every(c=>c.errorCode==='DEPENDENT_NOT_RUN'));
});

test('reader remains one shot after successful sample',async()=>{
 let calls=0;const p=createServerRouteWitness({readSnapshots:()=>{calls++;return pair();}});
 assert.equal(await p.begin(),true);assert.equal((await p.sample()).valid,true);
 assert.equal(await p.begin(),false);assert.equal((await p.sample()).valid,false);assert.equal(calls,2);
});
