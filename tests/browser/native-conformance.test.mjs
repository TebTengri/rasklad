// Project license pending; see README.md
// Independently generated native gold; no real model, dictionary or user input.
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const require=createRequire(import.meta.url);
execFileSync(fileURLToPath(new URL('../../bin/build-browser-fixture',import.meta.url)),{stdio:'ignore'});
const policy=require('../../build/browser-fixture/policy.js');
const {createRouter}=require('../../browser/router.js');
const {convert}=require('../../build/browser-fixture/converter.js');
const {loadModel}=require('../../browser/scorer.js');
const gold=JSON.parse(readFileSync(new URL('../../build/browser-fixture/synthetic-conformance.json',import.meta.url),'utf8'));
test('numeric browser scorer matches native on synthetic weights and boundary decisions',()=>{
  let scores=0,decisions=0;
  for(const entry of gold.models){
    const model=loadModel(new Uint8Array(Buffer.from(entry.bytesBase64,'base64')),policy.modelSpec);
    for(const item of entry.cases){
      const actual=model.score(item.keys);
      assert.deepEqual(actual,item.score,'native raw score parity');scores++;
      for(const layout of ['us','ru']){
        assert.equal(model.decide(actual,layout),item[layout],'native model action parity');decisions++;
      }
    }
    for(const item of gold.boundaries)for(const layout of ['us','ru']){
      assert.equal(model.decide(item.score,layout),item[layout],'native threshold boundary parity');decisions++;
    }
    model.dispose();
    assert.equal(model.score([0,1,2,3]).ok,false);
    assert.equal(model.decide(entry.cases[0].score,'us'),'abstain');
  }
  assert.equal(scores,1176);assert.equal(decisions,2464);
});

test('explicit native evidence oracle has no consumer-side bit or index formula',()=>{
  assert.equal(gold.routerCases.length,512);
  for(const row of gold.routerCases){
    const router=createRouter({policy,convert,evidence:()=>row.evidence});
    assert.deepEqual(router.route({field:'prose',layout:'us',token:row.token,profile:row.profile}),row.expected);
  }
});
test('converter matches native Unicode and UTF16 oracle',()=>{
  assert.equal(gold.converterCases.length,27);
  for(const row of gold.converterCases){
    const text=String.fromCharCode(...row.source),actual=convert(text,row.direction);
    assert.deepEqual(Array.from({length:actual.length},(_,i)=>actual.charCodeAt(i)),row.expected);
  }
});
