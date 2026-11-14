// SPDX-License-Identifier: MIT
// Unit fault injection for Claude's oracle/controller, NOT a browser or Wayland model proof.
import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {runFocusScenarios} from '../chromium_ime/adversarial-focus.mjs';
import {selectEndEventPolicy} from '../chromium_ime/focus-policy.mjs';

function fixture(mode='normal') {
  const allFields=[];
  const timers=new Set();
  const context=vm.createContext({AbortController});
  let composing=null;
  let bypass=false;
  let injected=false;
  const fields={};
  function field(id) {
    const listeners=new Map();
    const signals=new Map();
    const element={id,value:'',
      addEventListener(type, fn, options) {
        if(options?.signal?.aborted)return;
        if(!listeners.has(type))listeners.set(type,new Set());listeners.get(type).add(fn);
        if(options?.signal){const remove=()=>element.removeEventListener(type,fn);signals.set(fn,{signal:options.signal,remove});options.signal.addEventListener('abort',remove,{once:true});}
      },
      removeEventListener(type, fn) {listeners.get(type)?.delete(fn);const entry=signals.get(fn);if(entry){entry.signal.removeEventListener('abort',entry.remove);signals.delete(fn);}},
      focus() {
        const old=context.document.activeElement;
        if(old===element)return;
        if(old===composing){composing=null;emit(old,'compositionend');}
        context.document.activeElement=element;
      },
      listenerCount() {return [...listeners.values()].reduce((n,set)=>n+set.size,0);},
      listeners,
    };
    allFields.push(element);
    return element;
  }
  for(const id of ['prose','other','secret'])fields[id]=field(id);
  context.document={getElementById:id=>fields[id]||null,activeElement:fields.prose};
  context.window=context;
  context.imeEvents=[];
  context.setTimeout=(fn,delay)=>{
    const timer=setTimeout(()=>{timers.delete(timer);fn();},delay);timers.add(timer);return timer;
  };
  context.clearTimeout=timer=>{timers.delete(timer);clearTimeout(timer);};
  function emit(element,type) {
    if(type==='compositionend'&&['wrong_target_end','wrong_target_untrusted_end'].includes(mode))element=fields.other;
    const event={target:element,isTrusted:mode!=='untrusted_events'&&!(type==='compositionend'&&['native_untrusted_end','wrong_target_untrusted_end'].includes(mode)),type};
    context.imeEvents.push({type,inputType:'',length:0});
    for(const fn of [...(element.listeners.get(type)||[])])fn(event);
  }
  async function ev(expression) {
    const result=await vm.runInContext(expression,context,{timeout:250});
    return mode==='unexpected_snapshot_code'&&typeof result==='number'&&result>=100?200:result;
  }
  async function reset(id='prose') {
    composing=null;
    for(const element of Object.values(fields))element.value='';
    context.document.activeElement=fields[id];
    context.imeEvents=[];
  }
  async function type(command) {
    const active=context.document.activeElement;
    if(command==='word') {
      active.value='ghbdtn';
      if(!bypass){composing=active;emit(active,'compositionstart');}
    } else if(command==='escape') {
      if(mode!=='escape_ignored'&&composing){const old=composing;composing=null;bypass=true;if(mode!=='escape_end_missing')emit(old,'compositionend');}
    } else if(command==='space') {
      if(mode==='throw_on_space')throw Error('PRIVATE_SENTINEL_MUST_NOT_ESCAPE');
      if(bypass){bypass=false;active.value+=' ';}
      else if(composing) {
        const old=composing;composing=null;old.value='привет ';
        if(mode!=='hook_event_missing')emit(old,'compositionend');
        if(mode==='double_hook')emit(old,'compositionend');
      } else active.value+=mode==='cleanup_payload'&&context.__raskladFocus===undefined?'ghbdtn ':' ';
      if(mode==='state_deleted')delete context.__raskladFocus;
      if(mode==='state_cloned'&&context.__raskladFocus)context.__raskladFocus={...context.__raskladFocus};
      if(mode==='controller_aborted')context.__raskladFocusCtl?.abort();
      if(mode==='node_replaced'&&!injected){fields.other=field('other');injected=true;}
      if(mode==='word_lost')fields.prose.value='';
    } else throw Error('unsupported fixed command');
  }
  return {ev,type,reset,allFields,timers};
}

async function run(mode,policy) {
  const owned=fixture(mode);
  const report=await runFocusScenarios(owned,policy);
  return {owned,report};
}

test('strict valid paths pass and leave no listeners/timers',async()=>{
  const {owned,report}=await run();
  assert.equal(report.passed,true);
  assert.equal(report.checks,4);
  assert.equal(report.cases.length,4);
  assert.ok(report.cases.every(c=>c.precondition&&c.passed));
  assert.equal(owned.allFields.reduce((n,f)=>n+f.listenerCount(),0),0);
  assert.equal(owned.timers.size,0);
});

for(const mode of ['word_lost','hook_event_missing','double_hook','state_deleted','state_cloned','controller_aborted','node_replaced','wrong_target_end']) {
  test(`oracle rejects ${mode}`,async()=>{
    const {owned,report}=await run(mode);
    assert.equal(report.passed,false);
    assert.ok(report.cases.some(c=>c.passed===false));
    assert.equal(owned.allFields.reduce((n,f)=>n+f.listenerCount(),0),0);
    assert.equal(owned.timers.size,0);
  });
}

test('unexpected numeric snapshot code is rejected',async()=>{
  const {report}=await run('unexpected_snapshot_code');
  assert.equal(report.passed,false);
  assert.ok(report.cases.every(c=>c.passed===false));
});

test('untrusted events cannot establish composition preconditions',async()=>{
  const {report}=await run('untrusted_events');
  assert.equal(report.passed,false);
  assert.ok(report.cases.every(c=>c.precondition===false&&c.passed===false));
});

test('ignored Escape cannot be rescued by a later blur',{timeout:5000},async()=>{
  const {owned,report}=await run('escape_ignored');
  const cancellation=report.cases.find(c=>c.id==='word_escape_other_space');
  assert.ok(cancellation);
  assert.equal(cancellation.precondition,true);
  assert.equal(cancellation.passed,false);
  assert.equal(owned.timers.size,0);
});

test('errors are fixed codes and cleanup survives them',async()=>{
  const {owned,report}=await run('throw_on_space');
  assert.equal(report.passed,false);
  assert.equal(JSON.stringify(report).includes('PRIVATE_SENTINEL'),false);
  assert.equal(owned.allFields.reduce((n,f)=>n+f.listenerCount(),0),0);
  assert.equal(owned.timers.size,0);
});

test('untrusted end is rejected by default but explicit fixture policy supports observed Chromium',{timeout:6000},async()=>{
  const strict=await run('native_untrusted_end');
  assert.equal(strict.report.passed,false);
  const scoped=await run('native_untrusted_end','controlled_chromium_scoped_end');
  assert.equal(scoped.report.passed,true);
  assert.equal(scoped.report.endEventPolicy,'controlled_chromium_scoped_end');
});

test('cleanup native boundary prevents Escape failure contaminating next case',{timeout:5000},async()=>{
  const {report}=await run('escape_end_missing');
  assert.equal(report.cases[2].passed,false);
  assert.equal(report.cases[2].cleanupPassed,true);
  assert.equal(report.cases[3].precondition,true);
  assert.equal(report.cases[3].passed,true);
});

test('cleanup payload is failure and later cases are explicitly not run',async()=>{
  const {report}=await run('cleanup_payload');
  assert.equal(report.passed,false);
  assert.equal(report.cases[0].cleanupPassed,false);
  assert.equal(report.executedCases,1);
  assert.ok(report.cases.slice(1).every(c=>c.errorCode==='DEPENDENT_NOT_RUN'&&c.executed===false));
});

test('scoped policy requires exact version, reviewed sources and valid positive control',()=>{
  const good={browserVersion:'Chrome/155.0.8059.84',sourcesReviewed:true,correctedOnce:true,
    control:{trustedSourceStarts:1,sourceEnds:1,totalStarts:1,totalEnds:1,trustedSourceEnds:0,untrustedSourceEnds:1}};
  assert.equal(selectEndEventPolicy(good),'controlled_chromium_scoped_end');
  for(const changed of [{sourcesReviewed:false},{correctedOnce:false},{browserVersion:'Chrome/156.0.0.0'},
    {control:{...good.control,totalEnds:2}},{control:{...good.control,trustedSourceStarts:0}}, {control:null}])
    assert.equal(selectEndEventPolicy({...good,...changed}),null);
});

test('scoped compatibility never lets another field close the source barrier',{timeout:6000},async()=>{
  const {report}=await run('wrong_target_untrusted_end','controlled_chromium_scoped_end');
  assert.equal(report.passed,false);
  assert.ok(report.cases.every(c=>c.passed===false));
});

test('unknown policy fails closed without actions or leaking its input',async()=>{
  let calls=0;
  const forbidden=()=>{calls++;throw Error('must not run');};
  const report=await runFocusScenarios({ev:forbidden,type:forbidden,reset:forbidden},'PRIVATE_POLICY_SENTINEL');
  assert.equal(report.passed,false);
  assert.equal(calls,0);
  assert.equal(JSON.stringify(report).includes('PRIVATE_POLICY_SENTINEL'),false);
});
