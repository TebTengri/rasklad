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
  assert.equal(snap.valid,true);assert.equal(snap.records.length,3);
  assert.equal(snap.records[1].targetIndex,-1);assert.equal(snap.records[1].keyCodeIs229,true);
  assert.equal(snap.records[2].inputTypeIndex,-1);
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

// The mock emits controlled events for fault tests only; live fixtures never do.
test('keyboard code classes retain no raw code and do not read key/data',async()=>{
  const f=fixture(),p=createSpaceTelemetry(f);await p.begin(ID);await p.arm();
  for(const code of ['Space','Unidentified','','PRIVATE_CODE'])f.emit('keydown',{code,keyCode:229});
  const s=await p.snapshot();assert.deepEqual(s.records.map(r=>r.codeClassIndex),[0,1,2,3]);
  assert.ok(s.records.every(r=>r.keyCodeIs229));assert.equal(JSON.stringify(s).includes('PRIVATE'),false);
  await p.stop();
});
test('input seen before await survives registration gap and wait is one shot',async()=>{
  const f=fixture(),p=createSpaceTelemetry(f);await p.begin(ID);await p.arm();
  f.emit('input',{inputType:'insertText'});
  const r=await p.awaitInputAfterFailure();assert.equal(r.valid,true);assert.equal(r.observedBeforeWait,true);
  assert.equal((await p.awaitInputAfterFailure()).valid,false);assert.equal(f.timers.size,0);await p.stop();
});
test('only actual input wakes wait, including unknown target',async()=>{
  const f=fixture(),p=createSpaceTelemetry(f);await p.begin(ID);await p.arm();
  const pending=p.awaitInputAfterFailure();assert.equal(f.timers.size,1);
  f.emit('beforeinput',{inputType:'insertText',defaultPrevented:true});assert.equal(f.timers.size,1);
  f.emitUnknown('input',{inputType:'insertText'});
  const r=await pending;assert.equal(r.observedDuringWait,true);assert.equal(f.timers.size,0);
  const s=await p.snapshot();assert.equal(s.records[1].targetIndex,-1);await p.stop();
});
test('beforeinput alone reaches bounded deadline, never signals successful delivery',async()=>{
  const f=fixture(),p=createSpaceTelemetry(f);await p.begin(ID);await p.arm();
  f.emit('beforeinput',{inputType:'insertText',defaultPrevented:true});
  const r=await p.awaitInputAfterFailure();assert.equal(r.valid,true);assert.equal(r.timedOut,true);
  assert.equal(r.observedDuringWait,false);assert.equal(f.timers.size,0);await p.stop();
});
for(const cut of ['snapshot','stop'])test(`${cut} cancels pending waiter and timer`,async()=>{
  const f=fixture(),p=createSpaceTelemetry(f);await p.begin(ID);await p.arm();
  const pending=p.awaitInputAfterFailure();assert.equal(f.timers.size,1);
  await p[cut]();const r=await pending;assert.equal(r.cancelled,true);assert.equal(f.timers.size,0);
  if(cut==='snapshot')await p.stop();assert.equal(countListeners(f),0);
});
test('overflow still signals input while complete remains false',async()=>{
  const f=fixture(),p=createSpaceTelemetry(f);await p.begin(ID);await p.arm();
  for(let i=0;i<32;i++)f.emit('keydown',{code:'Space',keyCode:32});
  const pending=p.awaitInputAfterFailure();f.emit('input',{inputType:'insertText'});
  const r=await pending;assert.equal(r.observedDuringWait,true);assert.equal(r.complete,false);
  const s=await p.snapshot();assert.equal(s.dropped,1);assert.equal(s.complete,false);await p.stop();
});
async function failedSpaceFixture(waitFault=false){
  const f=fixture(),p=createSpaceTelemetry(f);let suppressed=false;
  const type=async name=>{
    if(name==='space'&&!suppressed&&await f.ev(`window.__raskladFocus?.token===${JSON.stringify(ID)}`)){
      suppressed=true;return;
    }
    return f.type(name);
  };
  const telemetry={...p,awaitInputAfterFailure:async()=>{
    if(waitFault)throw Error('PRIVATE_WAIT_SENTINEL');
    const pending=p.awaitInputAfterFailure();
    await f.ev(`document.getElementById('prose').value='ghbdtn '`);
    f.emit('input',{inputType:'insertText'});return pending;
  }};
  const r=await runFocusScenarios({...f,type,spaceTelemetry:telemetry});
  assert.equal(f.timers.size,0);assert.equal(countListeners(f),0);return r;
}
test('late exact match never rescues original FAIL and cleanup input is excluded',async()=>{
  const r=await failedSpaceFixture(),c=r.cases[1];assert.equal(r.passed,false);assert.equal(c.passed,false);
  assert.equal(c.errorCode,'ASSERTION_FAILED');assert.equal(c.lateDelivery.primarySnapshotCode,0);
  assert.equal(c.lateDelivery.waitStatus,'input_during_wait');assert.equal(c.lateDelivery.lateSnapshotCode,100);
  assert.equal(c.lateDelivery.lateMatchesOriginalOracle,true);assert.equal(c.cleanupPassed,true);
  assert.deepEqual(c.spaceTelemetry.records.map(x=>x.typeIndex),[3]);
  assert.ok(r.cases.filter((_,i)=>i!==1).every(x=>x.passed));
});
test('wait errors export no exception text and block dependent cases',async()=>{
  const r=await failedSpaceFixture(true),c=r.cases[1];assert.equal(c.passed,false);
  assert.equal(c.lateDelivery.waitStatus,'error');assert.equal(c.cleanupPassed,false);
  assert.equal(JSON.stringify(r).includes('PRIVATE_WAIT_SENTINEL'),false);
  assert.ok(r.cases.slice(2).every(x=>x.errorCode==='DEPENDENT_NOT_RUN'));
});
