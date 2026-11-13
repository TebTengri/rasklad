// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const require=createRequire(import.meta.url);
execFileSync(fileURLToPath(new URL('../../bin/build-browser-fixture',import.meta.url)),{stdio:'ignore'});
const policy=require('../../build/browser-fixture/policy.js');
const {convert}=require('../../build/browser-fixture/converter.js');
const {loadModel}=require('../../browser/scorer.js');
const {createEvidenceProvider}=require('../../browser/evidence.js');
const spec=policy.modelSpec;
function weights(en=-32,ru=-16) {
  const raw=new Uint8Array(spec.serializedBytes),v=new DataView(raw.buffer);
  raw.set([82,83,75,78]);
  [spec.version,spec.scale,spec.alphabet,spec.tableSize].forEach((n,i)=>v.setUint32(4+i*4,n,true));
  raw.fill(en&255,20,20+spec.tableSize);raw.fill(ru&255,20+spec.tableSize);return raw;
}
const copySpec=()=>JSON.parse(JSON.stringify(spec));
const query=(token='Qzxv',layout='us')=>({token,layout,keys:[...token].map(c=>policy.canonical[layout][c])});
const empty=()=>false;
function provider(overrides={}) {return createEvidenceProvider({model:loadModel(weights(),spec),policy,convert,
  lookup:{full:empty,control:empty,stem:empty},...overrides});}
test('flat weights give expected scores, decisions and short-token abstention',()=>{
  const m=loadModel(weights(),spec);
  for(let n=1;n<=32;n++){
    const s=m.score(new Array(n).fill(0));
    assert.deepEqual(s,{ok:true,keyCount:n,steps:n+1,raw:[-96*(n+1),-48*(n+1)]});
    assert.equal(m.decide(s,'us'),n<4?'abstain':'correct');
    assert.equal(m.decide(s,'ru'),n<4?'abstain':'keep');
  }
  assert.equal(loadModel(weights(-24,-24),spec).decide(m.score([0,1,2,3]),'unknown'),'abstain');
});
test('format failures do not disclose model content',()=>{
  const variants=[];
  variants.push(weights().slice(1));
  for(const [offset,value] of [[0,0],[4,2],[8,9],[12,36],[16,0],[20,128],[20,1]]){
    const b=weights();b[offset]=value;variants.push(b);
  }
  for(const b of variants)assert.throws(()=>loadModel(b,spec),TypeError);
  for(const b of [null,[],new ArrayBuffer(spec.serializedBytes),new DataView(weights().buffer)])
    assert.throws(()=>loadModel(b,spec),TypeError);
  if(typeof SharedArrayBuffer!=='undefined')assert.throws(()=>loadModel(new Uint8Array(new SharedArrayBuffer(spec.serializedBytes)),spec),TypeError);
});
test('malformed getters, detached and spoofed shared buffers produce generic errors',()=>{
  const bad=copySpec();Object.defineProperty(bad,'scale',{get(){throw Error('fictional private sentinel');}});
  assert.throws(()=>loadModel(weights(),bad),e=>e instanceof TypeError && e.message==='invalid model');
  const detached=weights();structuredClone(detached.buffer,{transfer:[detached.buffer]});
  assert.throws(()=>loadModel(detached,spec),TypeError);
  if(typeof SharedArrayBuffer!=='undefined'){
    const buffer=new SharedArrayBuffer(spec.serializedBytes);
    Object.defineProperty(buffer,Symbol.toStringTag,{value:'ArrayBuffer'});
    assert.throws(()=>loadModel(new Uint8Array(buffer),spec),TypeError);
  }
});
test('bounded owned subarray snapshot and disposal',()=>{
  const backing=new Uint8Array(spec.serializedBytes+16),input=backing.subarray(8,8+spec.serializedBytes);
  input.set(weights());const mutableSpec=copySpec(),m=loadModel(input,mutableSpec);
  const before=m.score([0,1,2,3]);input.fill(0);mutableSpec.twoScore.enabled=false;mutableSpec.scale=999;
  assert.deepEqual(m.score([0,1,2,3]),before);assert.equal(m.decide(before,'us'),'correct');
  m.dispose();m.dispose();assert.equal(m.score([0,1,2,3]).ok,false);assert.equal(m.decide(before,'us'),'abstain');
});
test('invalid keys and hand-constructed scores fail closed',()=>{
  const m=loadModel(weights(),spec);
  for(const keys of [[],Array(33).fill(0),[0,1,2,33],[0,1,2,-1],[0,1,2,NaN],[0,1,2,'3'],null,{length:4}])
    assert.equal(m.score(keys).ok,false);
  const valid=m.score([0,1,2,3]);
  for(const s of [null,{}, {...valid,ok:1},{...valid,keyCount:4.5},{...valid,steps:4},
    {...valid,raw:[0,1]},{...valid,raw:[0,-99999]},{...valid,raw:[0,NaN]},
    {...valid,raw:[0,-1.5]},{...valid,raw:[0,0,0]}])assert.equal(m.decide(s,'us'),'abstain');
});
test('disabled or invalid thresholds never correct',()=>{
  for(const patch of [{enabled:false},{gap:-1},{gap:NaN},{minAlternative:Infinity}]){
    const s=copySpec();Object.assign(s.twoScore,patch);
    // A loader may reject malformed config, or accept it and abstain like native.
    let m;try{m=loadModel(weights(),s);}catch(e){assert.ok(e instanceof TypeError);continue;}
    assert.equal(m.decide(m.score([0,1,2,3]),'us'),'abstain');
  }
});
test('exact full/control/stem membership, case and layout reach local callbacks',()=>{
  const seen=[];
  const make=name=>(language,value)=>{seen.push([name,language,value]);return language==='ru';};
  const evidence=provider({lookup:{full:make('full'),control:make('control'),stem:make('stem')}});
  assert.deepEqual(evidence(query()),{full:{observed:false,alternative:true},control:{observed:false,alternative:true},
    stem:{observed:false,alternative:true},model:'correct'});
  assert.equal(seen[0][2],'Qzxv');assert.equal(seen[1][2],convert('Qzxv','us-ru'));
  assert.ok(Object.isFrozen(seen[4][2]));
  seen.length=0;
  const reverse=evidence(query('Йячм','ru'));assert.equal(reverse.full.observed,true);assert.equal(reverse.model,'keep');
  assert.equal(seen[0][1],'ru');assert.equal(seen[1][1],'en');
});
test('provider rejects malformed queries before dictionary access',()=>{
  let calls=0;const count=()=>{calls++;return false;};const e=provider({lookup:{full:count,control:count,stem:count}});
  const q=query();
  for(const bad of [null,{},query('abc'),{...q,layout:'unknown'},{...q,keys:[0,0,0,0]},
    {...q,token:'a'.repeat(33)},{...q,keys:[1,2,3,'4']},{...q,token:'a😀a'}])assert.equal(e(bad),null);
  assert.equal(calls,0);
});
test('missing, throwing, asynchronous or nonboolean dictionary never becomes false evidence',()=>{
  assert.throws(()=>provider({lookup:{full:empty}}),TypeError);
  assert.throws(()=>provider({model:{}}),TypeError);
  for(const invalid of [()=>undefined,()=>null,()=>0,()=>Promise.resolve(false),()=>{throw Error('fictional');}])
    assert.equal(provider({lookup:{full:invalid,control:empty,stem:empty}})(query()),null);
});
test('bad converter, model response and mutable source keys fail closed',()=>{
  for(const bad of [()=>null,()=> 'xxxx',()=>{throw Error('fixture');}])assert.equal(provider({convert:bad})(query()),null);
  assert.equal(provider({model:{score:()=>({}),decide:()=>Promise.resolve('correct')}})(query()),null);
  for(const raw of [[0,-99999],[0,NaN],[0,1],[0,-1.5]])
    assert.equal(provider({model:{score:()=>({ok:true,keyCount:4,steps:5,raw}),decide:()=> 'correct'}})(query()),null);
  const q=query();
  const e=provider({lookup:{full:()=>{q.keys.fill(32);return false;},control:empty,stem:empty}});
  assert.equal(e(q).model,'correct'); // Provider owns a frozen key snapshot before callbacks.
});
