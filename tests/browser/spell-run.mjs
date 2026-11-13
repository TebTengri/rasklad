// SPDX-License-Identifier: MIT
// Explicit candidate-only test: no download/install and no real dictionaries.
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {dirname,resolve} from 'node:path';
import assert from 'node:assert/strict';
import {startIsolatedChromium} from './isolated-chromium.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
const lock=JSON.parse(readFileSync(resolve(root,'tools/browser_spell/source-lock.json'),'utf8'));
function pinned(path,sha){const bytes=readFileSync(resolve(root,path));assert.equal(createHash('sha256').update(bytes).digest('hex'),sha,'candidate hash mismatch');return bytes;}
const loader=pinned(lock.loader_path,lock.loader_sha256),wasm=pinned(lock.wasm_path,lock.wasm_sha256);
// Inspect the actual WASM memory section, not just compiler flags.
let offset=8;const leb=()=>{let n=0,shift=0,b;do{assert.ok(offset<wasm.length&&shift<35);b=wasm[offset++];n+=(b&127)*2**shift;shift+=7;}while(b&128);return n;};
let memorySeen=false;
while(offset<wasm.length){const id=wasm[offset++],size=leb(),end=offset+size;assert.ok(end<=wasm.length);
 if(id===5){assert.equal(leb(),1);const flags=leb();assert.equal(flags,1);assert.equal(leb()*65536,16777216);assert.equal(leb()*65536,lock.maximum_memory_bytes);memorySeen=true;}
 offset=end;
}
assert.ok(memorySeen);
assert.equal(lock.release_approved,false); // Deliberately not a release profile.
const native=JSON.parse(readFileSync(resolve(root,'build/browser-spell-native/native.json'),'utf8'));
assert.equal(native.nativeVersion,lock.native_reference_version);
const modelGold=JSON.parse(readFileSync(resolve(root,'build/browser-fixture/synthetic-conformance.json'),'utf8'));
const spellSource=readFileSync(resolve(root,'browser/spell.js'),'utf8');
const browser=startIsolatedChromium(root);const {call}=browser;let passed=0;
try{
  const version=await call('Browser.getVersion');
  const {targetId}=await call('Target.createTarget',{url:'about:blank'});
  const {sessionId}=await call('Target.attachToTarget',{targetId,flatten:true});
  const c=(method,params)=>call(method,params,sessionId);
  await c('Page.enable');
  await Promise.all([browser.waitForEvent('Page.loadEventFired',sessionId),c('Page.navigate',{url:'file:///lab/build/browser-fixture/fixture.html'})]);
  async function evaluate(expression){const r=await c('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
    if(r.exceptionDetails)throw Error('spell fixture evaluation failed: '+r.exceptionDetails.text);return r.result.value;}
  await evaluate(`new Promise((resolve,reject)=>{const end=Date.now()+5000;(function ready(){if(globalThis.fixtureAdapter)return resolve(true);if(Date.now()>end)return reject(Error('fixture unavailable'));setTimeout(ready,20);})();})`);
  await evaluate('fixtureAdapter.dispose();');
  await evaluate(spellSource);
  const init=await evaluate('('+async function(config){
    const from64=s=>Uint8Array.from(atob(s),c=>c.charCodeAt(0));
    globalThis.networkAttempts=0;
    globalThis.fetch=()=>{networkAttempts++;throw Error('network disabled');};
    globalThis.XMLHttpRequest=function(){networkAttempts++;throw Error('network disabled');};
    const start=performance.now();
    const {default:factory}=await import(config.loaderUrl);
    const m=await factory({instantiateWasm:(imports,receive)=>{const instance=new WebAssembly.Instance(new WebAssembly.Module(from64(config.wasm)),imports);receive(instance);return instance.exports;},locateFile:()=> 'disabled.wasm',print:()=>{},printErr:()=>{}});
    const ptr=m._Hunspell_get_library_version();
    if(!Number.isInteger(ptr)||ptr<=0||ptr+6>m.HEAPU8.length||Array.from(m.HEAPU8.slice(ptr,ptr+6)).join(',')!=='49,46,55,46,53,0')throw Error('engine version mismatch');
    const profile=Object.fromEntries(Object.entries(config.native.profiles).map(([lang,p])=>[lang,{aff:from64(p.affBase64),dic:from64(p.dicBase64)}]));
    const engines=RaskladSpell.createEngines(m,profile);
    let spellCases=0,controls=0;
    for(const [lang,p] of Object.entries(config.native.profiles))for(const row of p.cases){
      if(engines.full(lang,row.token)!==row.full || engines.control(lang,row.token)!==row.control)throw Error('native spell mismatch');spellCases++;
    }
    for(const row of config.native.controlCases){let got;try{got=RaskladSpell.removeAffixRows(from64(row.sourceBase64));}catch{}
      if(Boolean(got)!==row.accepted)throw Error('control admission mismatch');
      if(got){const expected=from64(row.expectedBase64);if(got.length!==expected.length||got.some((v,i)=>v!==expected[i]))throw Error('control bytes mismatch');}controls++;
    }
    const model=RaskladScorer.loadModel(from64(config.model),RaskladPolicyData.modelSpec);
    const stemEn=RaskladLexicon.loadLexicon(from64(config.stem)),stemRu=RaskladLexicon.loadLexicon(from64(config.stem));
    const evidence=RaskladEvidence.createEvidenceProvider({model,policy:RaskladPolicyData,convert:RaskladConverter.convert,
      lookup:{full:engines.full,control:engines.control,stem:(lang,keys)=>(lang==='en'?stemEn:stemRu).contains(keys)}});
    globalThis.spellFixtureRouter=RaskladRouter.createRouter({policy:RaskladPolicyData,convert:RaskladConverter.convert,evidence});
    globalThis.spellFixtureAdapter=RaskladBrowser.createAdapter({document,decide:spellFixtureRouter.decideUs});
    spellFixtureAdapter.register(document.getElementById('prose'),{role:'prose',layout:'us'});
    globalThis.spellFixtureEngines=engines;
    return {spellCases,controls,setupAndChecksMs:performance.now()-start,heapBytes:engines.stats().heapBytes,engineCount:engines.stats().engineCount,networkAttempts};
  }.toString()+')('+JSON.stringify({loaderUrl:'data:text/javascript;base64,'+loader.toString('base64'),wasm:wasm.toString('base64'),native,model:modelGold.models[6].bytesBase64,stem:modelGold.lexicons.cases[modelGold.lexicons.fixtureIndex].bytesBase64})+')');
  assert.equal(init.spellCases,93);assert.equal(init.controls,7);assert.equal(init.engineCount,4);assert.equal(init.networkAttempts,0);passed+=4;
  async function type(text){for(const ch of text){await c('Input.dispatchKeyEvent',{type:'keyDown',key:ch,text:ch,unmodifiedText:ch});await c('Input.dispatchKeyEvent',{type:'keyUp',key:ch});}}
  for(const [input,expected] of [['ghbdtn x','привет x'],['Ghbdtn ','Привет '],['GHBDTN ','ПРИВЕТ '],['rjns ','коты '],['books ','books '],['rainbow ','rainbow ']]){
    await evaluate(`globalThis.field=document.getElementById('prose');field.value='';field.focus();`);await type(input);
    assert.equal(await evaluate('field.value'),expected,'full/control pipeline input integrity');passed++;
  }
  await c('Input.dispatchKeyEvent',{type:'rawKeyDown',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:2});
  await c('Input.dispatchKeyEvent',{type:'keyUp',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:2});
  await evaluate(`field.value='';field.focus();`);await type('ghbdtn ');
  await c('Input.dispatchKeyEvent',{type:'rawKeyDown',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:2});
  await c('Input.dispatchKeyEvent',{type:'keyUp',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:2});
  assert.equal(await evaluate('field.value'),'ghbdtn');passed++;
  await evaluate('spellFixtureEngines.dispose();');
  await evaluate(`field.value='';field.focus();`);await type('ghbdtn ');assert.equal(await evaluate('field.value'),'ghbdtn ');passed++;
  console.log(JSON.stringify({passed,browser:version.product,init,candidate:'Hunspell@'+lock.version,releaseApproved:false,scope:'synthetic aff/dic/model only; source-built Hunspell 1.7.5; matching repeated builds'}));
}catch(error){console.error(JSON.stringify({passed,error:error.message}));process.exitCode=1;}
finally{try{await browser.close();}catch(error){console.error(JSON.stringify({error:error.message}));process.exitCode=1;}}
