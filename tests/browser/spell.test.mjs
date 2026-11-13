// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync} from 'node:fs';
const require=createRequire(import.meta.url);
const {createEngines,removeAffixRows}=require('../../browser/spell.js');
const gold=JSON.parse(readFileSync(new URL('../../build/browser-spell-native/native.json',import.meta.url),'utf8'));
const bytes=s=>new Uint8Array(Buffer.from(s,'base64'));
const profile=()=>Object.fromEntries(Object.entries(gold.profiles).map(([lang,p])=>[lang,{aff:bytes(p.affBase64),dic:bytes(p.dicBase64)}]));
function stub(failCreate=0){
  const state={created:0,destroyed:0,spells:0,free:0,trap:false,last:null};let next=128;
  const files=new Map(),dirs=new Set();
  const m={HEAPU8:new Uint8Array(65536),FS:{mkdir:p=>dirs.add(p),writeFile:(p,b)=>files.set(p,b.slice()),unlink:p=>files.delete(p),rmdir:p=>dirs.delete(p)},
    _malloc:n=>{const ptr=next;next+=n+8;return ptr;},_free:()=>{state.free++;},
    _Hunspell_create:()=>{state.created++;return state.created===failCreate?0:m._malloc(8);},
    _Hunspell_get_dic_encoding:()=>8,_Hunspell_destroy:()=>{state.destroyed++;},
    _Hunspell_spell:(_handle,ptr)=>{state.spells++;if(state.trap)throw Error('fictional private sentinel');let end=ptr;while(m.HEAPU8[end])end++;state.last={ptr,size:end-ptr+1};return 1;}};
  m.HEAPU8.set(new TextEncoder().encode('UTF-8\0'),8);
  return {m,state,files,dirs};
}
test('control derivation matches native bytes including CRLF/BOM/flags and refusals',()=>{
  for(const row of gold.controlCases){
    if(row.accepted)assert.deepEqual(removeAffixRows(bytes(row.sourceBase64)),bytes(row.expectedBase64));
    else assert.throws(()=>removeAffixRows(bytes(row.sourceBase64)),TypeError);
  }
  assert.throws(()=>removeAffixRows(new Uint8Array(1024*1024+1)),TypeError);
});
test('four handles share a heap, temporary FS removed, query bytes wiped and dispose idempotent',()=>{
  const {m,state,files,dirs}=stub(),engines=createEngines(m,profile());
  assert.equal(engines.stats().engineCount,4);assert.equal(files.size,0);assert.equal(dirs.size,0);
  assert.equal(engines.full('en','book'),true);assert.equal(engines.control('ru','привет'),true);
  assert.ok(m.HEAPU8.slice(state.last.ptr,state.last.ptr+state.last.size).every(n=>n===0));
  const before=state.spells;for(const bad of ['',null,'a'.repeat(33),'book\n','\ud800'])assert.equal(engines.full('en',bad),false);
  assert.equal(state.spells,before);engines.dispose();engines.dispose();assert.equal(state.destroyed,4);
  assert.throws(()=>engines.full('en','book'),/spell unavailable/);
});
test('constructor failure cleans already-created handles, FS and pointers',()=>{
  const {m,state,files,dirs}=stub(3);assert.throws(()=>createEngines(m,profile()),e=>e.message==='invalid spell profile');
  assert.equal(state.destroyed,2);assert.equal(files.size,0);assert.equal(dirs.size,0);assert.ok(state.free>0);
});
test('native trap disables further lookups without error text disclosure',()=>{
  const {m,state}=stub(),engines=createEngines(m,profile());state.trap=true;
  assert.throws(()=>engines.full('en','book'),e=>e.message==='spell unavailable');
  assert.equal(engines.stats().status,'failed');const n=state.spells;
  assert.throws(()=>engines.control('ru','привет'),/spell unavailable/);assert.equal(state.spells,n);engines.dispose();
});
test('profile must be UTF8/noNUL/nonshared and preinitialized module',()=>{
  const {m}=stub();const p=profile();p.en.aff=new Uint8Array([255]);assert.throws(()=>createEngines(m,p),TypeError);
  const p2=profile();p2.en.dic=new Uint8Array([0]);assert.throws(()=>createEngines(m,p2),TypeError);
  assert.throws(()=>createEngines(Promise.resolve(m),profile()),TypeError);
  if(typeof SharedArrayBuffer!=='undefined'){const p3=profile();p3.en.aff=new Uint8Array(new SharedArrayBuffer(10));assert.throws(()=>createEngines(m,p3),TypeError);}
});
