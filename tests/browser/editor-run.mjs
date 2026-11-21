// SPDX-License-Identifier: MIT
// Closed owner-side editor integration; no host profile, accounts or user text.
import {readFileSync} from 'node:fs';
import {dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
import {startIsolatedChromium} from './isolated-chromium.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');
let stage='input',browser;
async function main(){
 const chunks=[];let length=0;
 for await(const chunk of process.stdin){length+=chunk.length;if(length>24*1024*1024)throw Error('input');chunks.push(chunk);}
 const config=JSON.parse(Buffer.concat(chunks).toString('utf8'));
 assert.equal(config.protocol.schema,1);assert.equal(config.protocol.cases.length,12);
 stage='browser';browser=startIsolatedChromium(root,65000,'/out/editor-fixture');
 const {product}=await browser.call('Browser.getVersion');
 const {targetId}=await browser.call('Target.createTarget',{url:'about:blank'});
 const {sessionId}=await browser.call('Target.attachToTarget',{targetId,flatten:true});
 const c=(method,params)=>browser.call(method,params,sessionId);
 await c('Page.enable');
 await Promise.all([browser.waitForEvent('Page.loadEventFired',sessionId),c('Page.navigate',{url:'file:///lab/build/browser-fixture/editor.html'})]);
 async function evaluate(expression){const r=await c('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error('evaluation');return r.result.value;}
 stage='own-code';
 for(const file of ['build/browser-fixture/converter.js','build/browser-fixture/policy.js','browser/router.js','browser/scorer.js',
                    'browser/lexicon.js','browser/spell.js','browser/evidence.js','browser/adapter.js','browser/session.js'])await evaluate(readFileSync(resolve(root,file),'utf8'));
 stage='profile-init';
 const init=await evaluate('('+async function(config){
  const from64=s=>Uint8Array.from(atob(s),c=>c.charCodeAt(0));
  const to64=b=>{let s='';for(let i=0;i<b.length;i+=8192)s+=String.fromCharCode(...b.subarray(i,i+8192));return btoa(s);};
  globalThis.editorNetworkAttempts=0;globalThis.editorInstantiateCount=0;
  globalThis.fetch=()=>{editorNetworkAttempts++;throw Error('offline');};
  globalThis.XMLHttpRequest=function(){editorNetworkAttempts++;throw Error('offline');};
  const assets=Object.fromEntries(Object.entries(config.assets).map(([name,s])=>[name,from64(s)]));
  const digest=async bytes=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),b=>b.toString(16).padStart(2,'0')).join('');
  const instantiate=async({loader,wasm})=>{
   editorInstantiateCount++;
   const {default:factory}=await import('data:text/javascript;base64,'+to64(loader));
   return await factory({instantiateWasm(imports,receive){const instance=new WebAssembly.Instance(new WebAssembly.Module(wasm),imports);receive(instance);return instance.exports;},
    locateFile(){throw Error('offline');},print(){},printErr(){}});
  };
  globalThis.editorOptions={manifest:config.manifest,assets,digest,instantiate,document,policy:RaskladPolicyData,convert:RaskladConverter.convert,
   dependencies:{scorer:RaskladScorer,lexicon:RaskladLexicon,spell:RaskladSpell,evidence:RaskladEvidence,router:RaskladRouter,adapter:RaskladBrowser}};
  const start=performance.now();globalThis.editorSession=await RaskladSession.createSession(editorOptions);
  const initMs=performance.now()-start;
  for(const id of ['prose','code','protected'])editorSession.register(document.getElementById(id),{role:'prose',layout:'us'});
  return{initMs,stats:editorSession.stats(),networkAttempts:editorNetworkAttempts,instances:editorInstantiateCount};
 }.toString()+')('+JSON.stringify(config)+')');
 assert.equal(init.stats.status,'ready');assert.equal(init.stats.engineCount,4);assert.equal(init.stats.modelBytes,88290);assert.equal(init.instances,1);
 let cases=0;
 async function focus(id){await evaluate(`globalThis.editorField=document.getElementById(${JSON.stringify(id)});editorField.value='';editorField.focus();`);}
 async function type(text){for(const ch of text){await c('Input.dispatchKeyEvent',{type:'keyDown',key:ch,text:ch,unmodifiedText:ch});await c('Input.dispatchKeyEvent',{type:'keyUp',key:ch});}}
 for(const row of config.protocol.cases){
  assert.match(row.id,/^[a-z-]+$/);stage='case-'+row.id;await focus(row.field);await type(row.input);
  assert.equal(await evaluate('editorField.value'),row.expected);cases++;
 }
 stage='native-undo';await focus('prose');await type(config.protocol.undo.input);
 await c('Input.dispatchKeyEvent',{type:'rawKeyDown',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:2});
 await c('Input.dispatchKeyEvent',{type:'keyUp',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:2});
 assert.equal(await evaluate('editorField.value'),config.protocol.undo.expected);
 stage='dispose';const ready=await evaluate('editorSession.stats()');await evaluate('editorSession.dispose();editorSession.dispose();');
 await focus('prose');await type(config.protocol.disposed.input);assert.equal(await evaluate('editorField.value'),config.protocol.disposed.expected);
 assert.equal(await evaluate('editorSession.stats().engineCount'),0);
 stage='integrity-rejection';
 const rejected=await evaluate(`(async()=>{const before=editorInstantiateCount;editorOptions.assets['model.rskn'][0]^=1;let refused=false;try{await RaskladSession.createSession(editorOptions);}catch{refused=true;}return{refused,instantiated:editorInstantiateCount-before};})()`);
 assert.equal(rejected.refused,true);assert.equal(rejected.instantiated,0);
 await focus('prose');await type(config.protocol.disposed.input);assert.equal(await evaluate('editorField.value'),config.protocol.disposed.expected);
 assert.equal(await evaluate('editorNetworkAttempts'),0);
 stage='done';
 return{passed:true,browser:product,cases,checks:cases+4,hashAndInitMs:Math.round(init.initMs*10)/10,heapBytes:ready.heapBytes,
  modelBytes:ready.modelBytes,stemBytes:ready.stemBytes,engineCount:ready.engineCount,networkAttempts:0,
  undoPassed:true,disposePassed:true,integrityRejectionPassed:true,realEditorSupport:false};
}
let report;
try{report=await main();}catch{report={passed:false,error:'editor-test-failed',stage};process.exitCode=1;}
finally{if(browser)try{await browser.close();}catch{report={passed:false,error:'editor-close-failed',stage};process.exitCode=1;}}
process.stdout.write(JSON.stringify(report)+'\n');
