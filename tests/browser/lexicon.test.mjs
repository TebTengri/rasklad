// SPDX-License-Identifier: MIT
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const require=createRequire(import.meta.url);
execFileSync(fileURLToPath(new URL('../../bin/build-browser-fixture',import.meta.url)),{stdio:'ignore'});
const {loadLexicon}=require('../../browser/lexicon.js');
const gold=JSON.parse(readFileSync(new URL('../../build/browser-fixture/synthetic-conformance.json',import.meta.url),'utf8')).lexicons;
const bytes=entry=>new Uint8Array(Buffer.from(entry.bytesBase64,'base64'));
test('RSKF native writer/query oracle including empty, block boundaries, NUL and max keys',()=>{
  let queries=0;
  for(const item of gold.cases){
    const lex=loadLexicon(bytes(item));
    for(const check of item.checks){assert.equal(lex.contains(check.keys),check.expected);queries++;}
    const stats=lex.stats();assert.ok(Object.isFrozen(stats));
    assert.equal(stats.indexBytes,stats.blockCount*37);
    lex.dispose();assert.equal(lex.contains([0,0]),false);
  }
  assert.equal(gold.cases.length,8);assert.ok(queries>1000);
});
test('mutated RSKF acceptance and exact membership match native parser',()=>{
  assert.equal(gold.mutations.length,469);
  for(const item of gold.mutations){
    let loaded;try{loaded=loadLexicon(bytes(item));}catch(e){assert.equal(e.message,'invalid lexicon');}
    assert.equal(Boolean(loaded),item.accepted);
    if(loaded){for(const check of item.checks)assert.equal(loaded.contains(check.keys),check.expected);loaded.dispose();}
  }
});
test('owned subarray snapshot, invalid query types, and disposal',()=>{
  const raw=bytes(gold.cases[4]),backing=new Uint8Array(raw.length+10),view=backing.subarray(5,5+raw.length);
  view.set(raw);const lex=loadLexicon(view),positive=gold.cases[4].checks.find(x=>x.expected).keys;
  view.fill(0);assert.equal(lex.contains(positive),true);
  for(const key of [[],Array(33).fill(0),[0,-1],[33],[0,NaN],[0,1.1],['0'],null,{length:2}])assert.equal(lex.contains(key),false);
  const key=[...positive];Object.defineProperty(key,0,{get(){lex.dispose();return positive[0];}});
  assert.equal(lex.contains(key),false);lex.dispose();
  assert.deepEqual(lex.stats(),{keyCount:0,blockCount:0,payloadBytes:0,indexBytes:0});
});
test('shared/detached/proxy buffers and oversized files fail generically',()=>{
  const raw=bytes(gold.cases[1]);
  for(const bad of [null,[],raw.buffer,new DataView(raw.buffer),new Proxy(raw,{})])
    assert.throws(()=>loadLexicon(bad),e=>e instanceof TypeError&&e.message==='invalid lexicon');
  const detached=raw.slice();structuredClone(detached.buffer,{transfer:[detached.buffer]});assert.throws(()=>loadLexicon(detached),TypeError);
  if(typeof SharedArrayBuffer!=='undefined'){
    const b=new SharedArrayBuffer(raw.length);Object.defineProperty(b,Symbol.toStringTag,{value:'ArrayBuffer'});
    assert.throws(()=>loadLexicon(new Uint8Array(b)),TypeError);
  }
  assert.throws(()=>loadLexicon(new Uint8Array(64*1024*1024+1)),TypeError);
});
