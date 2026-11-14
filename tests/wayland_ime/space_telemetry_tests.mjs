// SPDX-License-Identifier: MIT
// Privacy/phase fault tests on an owned DOM mock, not browser delivery proof.
import test from 'node:test';
import assert from 'node:assert/strict';
import {fixture} from './focus_fixture.mjs';
import {createSpaceTelemetry,SPACE_TELEMETRY_CASE_ID as ID} from '../chromium_ime/space-telemetry.mjs';
import {runFocusScenarios} from '../chromium_ime/adversarial-focus.mjs';

const countListeners=f=>f.allFields.reduce((n,field)=>n+field.listenerCount(),0);

test('wrong case/focus fail closed without a trace',async()=>{
  const f=fixture(),probe=createSpaceTelemetry(f);
  assert.equal(await probe.begin('PRIVATE_CASE_SENTINEL'),false);
  assert.equal(countListeners(f),0);
  await f.reset('other');
  assert.equal(await probe.begin(ID),true);
  assert.equal(await probe.arm(),false);
  const snap=await probe.snapshot();
  assert.equal(snap.valid,true);assert.equal(snap.armed,false);assert.equal(snap.records.length,0);
  assert.equal(await probe.stop(),true);assert.equal(countListeners(f),0);
});

test('inactive prefix is ignored, snapshot cuts phase and cannot rearm',async()=>{
  const f=fixture(),probe=createSpaceTelemetry(f);
  await probe.begin(ID);
  f.emit('keydown',{code:'Space',keyCode:32});
  f.emit('input',{inputType:'insertText'});
  const inactive=await probe.snapshot();
  assert.equal(inactive.records.length,0);assert.equal(inactive.armed,false);
  assert.equal(await probe.arm(),false);
  await probe.stop();
  await probe.begin(ID);assert.equal(await probe.arm(),true);
  f.emit('keydown',{code:'Space',keyCode:32});
  const cut=await probe.snapshot();assert.equal(cut.records.length,1);assert.equal(cut.armed,true);
  f.emit('keyup',{code:'Space',keyCode:32});
  assert.equal((await probe.snapshot()).records.length,1);
  assert.equal(await probe.arm(),false);
  await probe.stop();
});

test('unknown targets/types become enums and forbidden key/data/id getters are not read',async()=>{
  const f=fixture(),probe=createSpaceTelemetry(f);
  await probe.begin(ID);await probe.arm();
  f.emit('keydown',{code:'KeyA',keyCode:65});
  f.emitUnknown('keydown',{code:'Space',keyCode:229});
  f.emitUnknown('input',{inputType:'PRIVATE_TYPE_SENTINEL'});
  const snap=await probe.snapshot();
  assert.equal(snap.valid,true);assert.equal(snap.records.length,2);
  assert.equal(snap.records[0].targetIndex,-1);assert.equal(snap.records[0].keyCodeIs229,true);
  assert.equal(snap.records[1].inputTypeIndex,-1);
  assert.equal(JSON.stringify(snap).includes('PRIVATE'),false);
  await probe.stop();assert.equal(countListeners(f),0);
});

test('record cap and dropped counter mark incomplete evidence',async()=>{
  const f=fixture(),probe=createSpaceTelemetry(f);
  await probe.begin(ID);await probe.arm();
  for(let i=0;i<50;i++)f.emit('input',{inputType:'insertText'});
  const snap=await probe.snapshot();
  assert.equal(snap.valid,true);assert.equal(snap.records.length,32);
  assert.equal(snap.dropped,18);assert.equal(snap.complete,false);
  await probe.stop();
});

test('malformed numeric slot does not leave renderer as raw data',async()=>{
  const f=fixture(),probe=createSpaceTelemetry(f);
  await probe.begin(ID);await probe.arm();f.emit('keydown',{code:'Space',keyCode:32});
  await f.ev(`window.__raskladSpaceTelemetry.records[0].typeIndex='PRIVATE_SLOT_SENTINEL'`);
  const snap=await probe.snapshot();
  assert.equal(snap.valid,false);assert.deepEqual(snap.records,[]);
  assert.equal(JSON.stringify(snap).includes('PRIVATE'),false);
  await probe.stop();
});

test('integration observes one case2 Space, not prefix or cleanup Space',async()=>{
  const f=fixture(),probe=createSpaceTelemetry(f);
  const result=await runFocusScenarios({...f,spaceTelemetry:probe});
  assert.equal(result.passed,true);
  const row=result.cases.find(c=>c.id===ID);
  assert.equal(row.spaceTelemetry.valid,true);
  assert.deepEqual(row.spaceTelemetry.records.map(r=>r.typeIndex),[0,2,3,1]);
  assert.ok(row.spaceTelemetry.records.every(r=>r.targetIndex===0));
  assert.equal(row.spaceTelemetry.records.find(r=>r.typeIndex===3).valueClassIndex[0],3);
  assert.equal(result.cases.filter(c=>c.spaceTelemetry!==undefined).length,1);
  assert.equal(countListeners(f),0);assert.equal(f.timers.size,0);
});

for(const method of ['begin','arm','snapshot','stop']) {
  test(`${method} failure cannot turn a case into pass or leak error`,async()=>{
    const f=fixture(),probe=createSpaceTelemetry(f);
    const broken={...probe,[method]:async()=>{throw Error('PRIVATE_TELEMETRY_SENTINEL');}};
    try {
      const result=await runFocusScenarios({...f,spaceTelemetry:broken});
      assert.equal(result.passed,false);
      assert.equal(result.cases[1].passed,false);
      assert.equal(result.cases[1].cleanupPassed,false);
      assert.ok(result.cases.slice(2).every(c=>c.errorCode==='DEPENDENT_NOT_RUN'));
      assert.equal(JSON.stringify(result).includes('PRIVATE_TELEMETRY'),false);
    } finally {await probe.stop();}
    assert.equal(countListeners(f),0);
  });
}
