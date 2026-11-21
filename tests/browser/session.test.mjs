// SPDX-License-Identifier: MIT
// No actual dictionaries, models or user text; dependency stubs test admission/lifecycle.
import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const {createSession}=require('../../browser/session.js');
const hash=b=>createHash('sha256').update(b).digest('hex');
function fixture(){
 const names=['engine.js','engine.wasm','model.rskn','stems.rskp','packs/en/dictionary.aff','packs/en/dictionary.dic','packs/ru/dictionary.aff','packs/ru/dictionary.dic',
  'packs/en/notices/UPSTREAM.txt','packs/en/notices/MODIFICATIONS.txt','packs/ru/notices/UPSTREAM.txt','packs/ru/notices/MODIFICATIONS.txt','notices/MODEL-MODIFICATIONS.txt','notices/MODEL-METADATA.json'];
 const assets=Object.fromEntries(names.map(n=>[n,new Uint8Array([11])]));
 assets['model.rskn']=new Uint8Array(88290);assets['model.rskn'][0]=11;
 const pair=new Uint8Array(28);pair.set([82,83,75,80]);const view=new DataView(pair.buffer);
 for(const [o,n] of [[4,1],[8,2],[12,4],[16,4]])view.setUint32(o,n,true);
 pair.set([82,83,75,70],20);pair.set([82,83,75,70],24);assets['stems.rskp']=pair;
 const manifest={schema:1,id:'local-test',admission:{local_test:true,distribution:false},engine:{version:'1.7.5',loader:'engine.js',wasm:'engine.wasm'},model:'model.rskn',stems:'stems.rskp',
  languages:Object.fromEntries(['en','ru'].map(l=>[l,{locale:l==='en'?'en-US':'ru-RU',aff:`packs/${l}/dictionary.aff`,dic:`packs/${l}/dictionary.dic`}])),
  files:names.map(name=>({name,bytes:assets[name].length,sha256:hash(assets[name])}))};
 const state={init:0,constructed:[],disposed:[],engine:'ready',registered:0,trap:false,modelInput:null};let lex=0;
 const object=(name,extra={})=>{state.constructed.push(name);return{...extra,dispose(){state.disposed.push(name);}};};
 const dependencies={
  scorer:{loadModel(bytes,spec){assert.equal(bytes[0],11);assert.equal(spec.fixture,7);state.modelInput=bytes;return object('model');}},
  lexicon:{loadLexicon(){return object(++lex===1?'en':'ru',{contains:()=>false});}},
  spell:{createEngines(){return object('engines',{stats:()=>({status:state.engine,engineCount:state.engine==='ready'?4:0,heapBytes:16777216}),
   full:()=>{if(state.trap){state.engine='failed';throw Error('never disclose this query');}return true;},control:()=>true});}},
  evidence:{createEvidenceProvider({lookup}){return()=>{try{lookup.full('en','fictional');return 'converted';}catch{return null;}};}},
  router:{createRouter({evidence}){return{decideUs:()=>evidence()};}},
  adapter:{createAdapter(){return object('adapter',{register:()=>{state.registered++;},unregister:()=>{state.registered--;}});}}
 };
 const options={manifest,assets,digest:async b=>hash(b),instantiate:async()=>{state.init++;return{};},dependencies,policy:{modelSpec:{fixture:7}},convert:()=>'',document:{}};
 return{options,state,repin(name){const row=manifest.files.find(x=>x.name===name);row.sha256=hash(assets[name]);row.bytes=assets[name].length;}};
}

test('ready profile registers only explicitly, disposes in reverse and preserves input snapshots',async()=>{
 const {options,state}=fixture(),session=await createSession(options);
 assert.equal(state.registered,0);assert.equal(session.stats().status,'ready');assert.equal(session.stats().engineCount,4);
 assert.equal(options.assets['model.rskn'][0],11);assert.equal(state.modelInput[0],0); // own transfer buffers wiped only
 session.register({},{});assert.equal(state.registered,1);session.unregister({});assert.equal(state.registered,0);
 assert.equal(session.decideUs('fictional'),'converted');session.dispose();session.dispose();
 assert.deepEqual(state.disposed,['adapter','engines','ru','en','model']);assert.equal(session.decideUs('fictional'),null);
 assert.equal(session.stats().status,'disposed');assert.equal(session.stats().engineCount,0);
});
test('bad asset hash/extra content/missing notice denies before any code or component initialization',async()=>{
 for(const change of [o=>o.assets['engine.wasm'][0]++,o=>o.assets.extra=new Uint8Array([1]),o=>{delete o.assets['packs/en/notices/UPSTREAM.txt'];}]){
  const {options,state}=fixture();change(options);await assert.rejects(createSession(options),/^TypeError: unavailable local profile$/);
  assert.equal(state.init,0);assert.deepEqual(state.constructed,[]);
 }
});
test('distribution cannot be approved by local permission or asset metadata',async()=>{
 const {options,state}=fixture();options.purpose='distribution';await assert.rejects(createSession(options),TypeError);assert.equal(state.init,0);
 options.purpose='local-test';options.manifest.admission={local_test:false,distribution:true};await assert.rejects(createSession(options),TypeError);
 assert.equal(state.init,0);
});
test('caller changes during asynchronous hashing do not replace profile bytes, policy or factories',async()=>{
 const {options,state}=fixture();let release;const gate=new Promise(resolve=>release=resolve);let once=true;
 options.digest=async b=>{if(once){once=false;await gate;}return hash(b);};
 const pending=createSession(options);
 options.assets['model.rskn'].fill(99);options.policy.modelSpec.fixture=0;options.manifest.admission.local_test=false;
 options.dependencies.scorer.loadModel=()=>{throw Error('changed factory');};release();
 const session=await pending;assert.equal(state.init,1);assert.equal(session.stats().status,'ready');session.dispose();
});
test('shared, detached and wrong typed buffers are rejected before digest/init',async()=>{
 for(const replacement of [new Uint8Array(new SharedArrayBuffer(1)),new Uint8ClampedArray([11]),new DataView(new ArrayBuffer(1))]){
  const {options,state}=fixture();options.assets['engine.js']=replacement;await assert.rejects(createSession(options),TypeError);assert.equal(state.init,0);
 }
 const {options,state}=fixture();structuredClone(options.assets['engine.js'],{transfer:[options.assets['engine.js'].buffer]});
 await assert.rejects(createSession(options),TypeError);assert.equal(state.init,0);
});
test('invalid RSKP shape rolls back model before any WASM instantiation',async()=>{
 for(const mutate of [b=>b[4]=2,b=>b[8]=3,b=>b[12]=255,b=>b[24]=0]){
  const {options,state,repin}=fixture();mutate(options.assets['stems.rskp']);repin('stems.rskp');
  await assert.rejects(createSession(options),TypeError);assert.equal(state.init,0);assert.deepEqual(state.disposed,['model']);
 }
});
test('instantiate/adapter failures release previously acquired model/stems/engines',async()=>{
 for(const stage of ['wasm','adapter']){
  const {options,state}=fixture();
  if(stage==='wasm')options.instantiate=async()=>{throw Error('private detail');};
  else options.dependencies.adapter.createAdapter=()=>{throw Error('private detail');};
  await assert.rejects(createSession(options),/^TypeError: unavailable local profile$/);
  assert.deepEqual(state.disposed,stage==='wasm'?['ru','en','model']:['engines','ru','en','model']);
 }
});
test('engine failure during evidence immediately disables the whole session and input hooks',async()=>{
 const {options,state}=fixture(),session=await createSession(options);state.trap=true;
 assert.equal(session.decideUs('fictional'),null);assert.equal(session.stats().status,'failed');
 assert.deepEqual(state.disposed,['adapter','engines','ru','en','model']);
 session.dispose();assert.equal(state.disposed.length,5);assert.throws(()=>session.register({},{}),TypeError);
});
test('malformed manifest paths, duplicate pins and unsupported engine fail before init',async()=>{
 for(const change of [m=>m.engine.version='0',m=>m.files.push({...m.files[0]}),m=>m.files[0].name='../escape',
  m=>m.files[0].bytes=true,m=>m.files[0].bytes=33*1024*1024,m=>m.languages.fr={locale:'fr-FR'},m=>m.extra=true]){
  const {options,state}=fixture();change(options.manifest);await assert.rejects(createSession(options),TypeError);assert.equal(state.init,0);
 }
});

test('malformed model fails before executable engine initialization',async()=>{
 const {options,state,repin}=fixture();options.assets['model.rskn'][0]=0;repin('model.rskn');
 await assert.rejects(createSession(options),TypeError);assert.equal(state.init,0);assert.deepEqual(state.constructed,[]);
});
test('missing handles or excessive heap refuse the entire profile before registration',async()=>{
 for(const bad of [{status:'ready',engineCount:2,heapBytes:16777216},{status:'ready',engineCount:4,heapBytes:Infinity},
                   {status:'ready',engineCount:4,heapBytes:134217729}]){
  const {options,state}=fixture(),original=options.dependencies.spell.createEngines;
  options.dependencies.spell.createEngines=(...args)=>({...original(...args),stats:()=>bad});
  await assert.rejects(createSession(options),TypeError);
  assert.deepEqual(state.disposed,['engines','ru','en','model']);assert.equal(state.registered,0);
 }
});
