// SPDX-License-Identifier: MIT
// Real pinned ProseMirror APIs, closed profile, fixed fictional typing only.
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
import {startIsolatedChromium} from './isolated-chromium.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../..');let stage='input',browser;
async function main(){
 const chunks=[];let length=0;
 for await(const b of process.stdin){length+=b.length;if(length>24*1024*1024)throw Error('input');chunks.push(b);}
 const cfg=JSON.parse(Buffer.concat(chunks).toString('utf8'));
 const raw=readFileSync(resolve(root,'build/prosemirror-fixture/modules.json'));
 const provenance=JSON.parse(readFileSync(resolve(root,'build/prosemirror-fixture/sources.json'),'utf8'));
 assert.equal(createHash('sha256').update(raw).digest('hex'),provenance.modules_sha256);
 cfg.modules=JSON.parse(raw);stage='browser';browser=startIsolatedChromium(root,65000,'/out/editor-fixture');
 const {product}=await browser.call('Browser.getVersion');
 const {targetId}=await browser.call('Target.createTarget',{url:'about:blank'});
 const {sessionId}=await browser.call('Target.attachToTarget',{targetId,flatten:true});
 const c=(method,params)=>browser.call(method,params,sessionId);
 await c('Page.enable');await Promise.all([browser.waitForEvent('Page.loadEventFired',sessionId),c('Page.navigate',{url:'file:///lab/build/browser-fixture/editor.html'})]);
 async function evaluate(expression){const r=await c('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error('evaluation');return r.result.value;}
 stage='own-code';
 for(const file of ['build/browser-fixture/converter.js','build/browser-fixture/policy.js','browser/router.js','browser/scorer.js','browser/lexicon.js',
                    'browser/spell.js','browser/evidence.js','browser/prosemirror.js','browser/session.js'])await evaluate(readFileSync(resolve(root,file),'utf8'));
 stage='profile-init';
 const init=await evaluate('('+async function(cfg){
  globalThis.pmNetworkAttempts=0;
  globalThis.fetch=()=>{pmNetworkAttempts++;throw Error('offline');};
  globalThis.XMLHttpRequest=function(){pmNetworkAttempts++;throw Error('offline');};
  const imported={};for(const [name,url] of Object.entries(cfg.modules))imported[name]=await import(url);
  const state=imported['prosemirror-state'],hist=imported['prosemirror-history'],model=imported['prosemirror-model'];
  const basic=imported['prosemirror-schema-basic'].schema;
  const schema=new model.Schema({nodes:basic.spec.nodes.update('doc',{...basic.spec.nodes.get('doc'),attrs:{raskladRole:{default:'unknown'}}}),marks:basic.spec.marks.addToEnd('annotation',{inclusive:false,toDOM:()=>['span',{'data-fixture-annotation':'yes'},0],parseDOM:[{tag:'span[data-fixture-annotation]'}]})});
  const historyPlugin=hist.history();
  globalThis.pmDecisions=0;globalThis.pmClassifications=0;globalThis.pmHook=null;globalThis.pmModes={};globalThis.pmViews={};
  globalThis.pmModules=imported;globalThis.pmSchema=schema;
  globalThis.pmInstances=0;globalThis.pmExecCalls=0;
  document.execCommand=()=>{pmExecCalls++;throw Error('no DOM editing fallback');};
  const from64=s=>Uint8Array.from(atob(s),c=>c.charCodeAt(0));
  const to64=b=>{let s='';for(let i=0;i<b.length;i+=8192)s+=String.fromCharCode(...b.subarray(i,i+8192));return btoa(s);};
  const assets=Object.fromEntries(Object.entries(cfg.assets).map(([name,b])=>[name,from64(b)]));
  const digest=async b=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',b)),x=>x.toString(16).padStart(2,'0')).join('');
  const instantiate=async({loader,wasm})=>{
   pmInstances++;const {default:factory}=await import('data:text/javascript;base64,'+to64(loader));
   return await factory({instantiateWasm(imports,receive){const instance=new WebAssembly.Instance(new WebAssembly.Module(wasm),imports);receive(instance);return instance.exports;},locateFile(){throw Error('offline');},print(){},printErr(){}});
  };
  const start=performance.now();
  globalThis.pmSession=await RaskladSession.createSession({manifest:cfg.manifest,assets,digest,instantiate,document,policy:RaskladPolicyData,convert:RaskladConverter.convert,
   dependencies:{scorer:RaskladScorer,lexicon:RaskladLexicon,spell:RaskladSpell,evidence:RaskladEvidence,router:RaskladRouter,
    adapter:{createAdapter(args){globalThis.pmAdapter=RaskladProseMirror.createAdapter({...args,
     modules:{Plugin:state.Plugin,PluginKey:state.PluginKey,closeHistory:hist.closeHistory,isHistoryTransaction:hist.isHistoryTransaction},historyPlugin,
     classify(s){pmClassifications++;return s.doc.attrs.raskladRole;},
     decide(token){pmDecisions++;return pmHook?pmHook(token):args.decide(token);}});return pmAdapter;}}}});
  const initMs=performance.now()-start;
  globalThis.pmMakeDoc=(role='prose',block='paragraph',text='')=>schema.nodes.doc.create({raskladRole:role},schema.nodes[block].create(null,text?schema.text(text):null));
  globalThis.pmMakeView=(id,doc)=>{
   let view;
   view=new imported['prosemirror-view'].EditorView(document.getElementById(id),{
    state:state.EditorState.create({doc,selection:state.TextSelection.atEnd(doc),plugins:[historyPlugin,pmAdapter.plugin]}),
    handleKeyDown(v,e){if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='z')return (e.shiftKey?hist.redo:hist.undo)(v.state,v.dispatch);return false;},
    dispatchTransaction(tr){
     const automatic=tr.getMeta(pmAdapter.plugin)?.auto;
     if(automatic&&pmModes[id]==='filter')return;
     if(automatic&&pmModes[id]==='throw')throw Error('fictional owner failure');
     if(automatic&&pmModes[id]==='rewrite'){view.updateState(view.state.apply(view.state.tr.insertText('!')));return;}
     view.updateState(view.state.apply(tr));
    }});
   if(id!=='unknown')pmSession.register(view,{role:'prose',layout:'us'});
   return view;
  };
  for(const id of ['prose','unknown','code','protected']){pmModes[id]='normal';pmViews[id]=pmMakeView(id,pmMakeDoc(id==='code'?'code':'prose'));}
  globalThis.pmReset=(id='prose',role=id==='code'?'code':'prose',block='paragraph',text='')=>{
   const v=pmViews[id];pmAdapter.unregister(v);pmModes[id]='normal';pmHook=null;
   const doc=pmMakeDoc(role,block,text);
   v.updateState(state.EditorState.create({doc,selection:state.TextSelection.atEnd(doc),plugins:[historyPlugin,pmAdapter.plugin]}));
   if(id!=='unknown')pmSession.register(v,{role:'prose',layout:'us'});v.focus();
  };
  return{initMs,stats:pmSession.stats(),instances:pmInstances};
 }.toString()+')('+JSON.stringify(cfg)+')');
 assert.equal(init.stats.status,'ready');assert.equal(init.instances,1);assert.equal(init.stats.engineCount,4);
 let cases=0,advanced=0,before;
 async function type(text){for(const ch of text){await c('Input.dispatchKeyEvent',{type:'keyDown',key:ch,text:ch,unmodifiedText:ch});await c('Input.dispatchKeyEvent',{type:'keyUp',key:ch});}}
 async function reset(id='prose',role,block,text){await evaluate(`pmReset(${[id,role,block,text].map(x=>x===undefined?'undefined':JSON.stringify(x)).join(',')})`);}
 const text=()=>evaluate('pmViews.prose.state.doc.textContent');
 async function undo(redo=false){await c('Input.dispatchKeyEvent',{type:'rawKeyDown',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:redo?10:2});await c('Input.dispatchKeyEvent',{type:'keyUp',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:redo?10:2});}
 for(const row of cfg.protocol.cases){stage='case-'+row.id;await reset(row.field);await type(row.input);assert.equal(await evaluate(`pmViews[${JSON.stringify(row.field)}].state.doc.textContent`),row.expected);cases++;}
 stage='undo-correction';await reset();await type('ghbdtn ');await undo();assert.equal(await text(),'ghbdtn');advanced++;
 stage='undo-space-suppression';await type(' ');assert.equal(await text(),'ghbdtn ');advanced++;
 stage='following-letter-undo';await reset();await type('ghbdtn x');await undo();assert.equal(await text(),'привет ');await undo();assert.equal(await text(),'ghbdtn');advanced++;
 stage='redo-correction';await undo(true);assert.equal(await text(),'привет ');advanced++;
 stage='bold-preserved';await reset();await evaluate('pmViews.prose.dispatch(pmViews.prose.state.tr.setStoredMarks([pmSchema.marks.strong.create()]))');await type('ghbdtn ');
 assert.equal(await text(),'привет ');assert.equal(await evaluate('pmViews.prose.state.doc.firstChild.firstChild.marks.some(m=>m.type===pmSchema.marks.strong)'),true);advanced++;
 for(const [id,block,mark,attrs] of [['code-block','code_block',null,null],['inline-code','paragraph','code',null],['link-span','paragraph','link',{href:'https://example.invalid'}]]){
  stage=id;await reset('prose','prose',block);if(mark)await evaluate(`pmViews.prose.dispatch(pmViews.prose.state.tr.setStoredMarks([pmSchema.marks[${JSON.stringify(mark)}].create(${JSON.stringify(attrs)})]))`);
  const before=await evaluate('pmDecisions');await type('ghbdtn ');assert.equal(await text(),'ghbdtn ');assert.equal(await evaluate('pmDecisions'),before);advanced++;
 }
 stage='mixed-marks';await reset();await type('ghbdtn');await evaluate('pmViews.prose.dispatch(pmViews.prose.state.tr.addMark(1,4,pmSchema.marks.strong.create()))');await type(' ');assert.equal(await text(),'привет ');assert.equal(await evaluate('pmViews.prose.state.doc.rangeHasMark(1,4,pmSchema.marks.strong)&&!pmViews.prose.state.doc.rangeHasMark(4,7,pmSchema.marks.strong)'),true);advanced++;
 stage='noninclusive-annotation';await reset();await type('ghbdtn');await evaluate('pmViews.prose.dispatch(pmViews.prose.state.tr.addMark(1,7,pmSchema.marks.annotation.create()))');await type(' ');assert.equal(await text(),'привет ');assert.equal(await evaluate('pmViews.prose.state.doc.rangeHasMark(1,7,pmSchema.marks.annotation)&&!pmViews.prose.state.doc.nodeAt(7).marks.some(m=>m.type===pmSchema.marks.annotation)'),true);advanced++;
 stage='pending-code-mark';await reset();await type('ghbdtn');await evaluate('pmViews.prose.dispatch(pmViews.prose.state.tr.setStoredMarks([pmSchema.marks.code.create()]))');before=await evaluate('pmDecisions');await type(' ');assert.equal(await text(),'ghbdtn ');assert.equal(await evaluate('pmDecisions'),before);advanced++;
 stage='pending-bold-space';await reset();await type('ghbdtn');await evaluate('pmViews.prose.dispatch(pmViews.prose.state.tr.setStoredMarks([pmSchema.marks.strong.create()]))');await type(' ');assert.equal(await text(),'привет ');assert.equal(await evaluate('!pmViews.prose.state.doc.rangeHasMark(1,7,pmSchema.marks.strong)&&pmViews.prose.state.doc.nodeAt(7).marks.some(m=>m.type===pmSchema.marks.strong)'),true);advanced++;
 stage='state-reconfigure';await reset();await type('ghbdtn');await evaluate('pmViews.prose.updateState(pmViews.prose.state.reconfigure({plugins:[...pmViews.prose.state.plugins]}))');assert.equal(await evaluate('pmAdapter.status(pmViews.prose)'),'registered');await type(' ');assert.equal(await text(),'привет ');advanced++;
 stage='unknown-role';await reset('prose','unknown');before=await evaluate('pmDecisions');await type('ghbdtn ');assert.equal(await text(),'ghbdtn ');assert.equal(await evaluate('pmDecisions'),before);advanced++;
 stage='selection';await reset();await type('ghbdtn');await evaluate('pmViews.prose.dispatch(pmViews.prose.state.tr.setSelection(pmModules["prosemirror-state"].TextSelection.create(pmViews.prose.state.doc,1,4)))');before=await evaluate('pmDecisions');await type(' ');assert.equal(await text(),' dtn');assert.equal(await evaluate('pmDecisions'),before);advanced++;
 stage='stale-state';await reset();await type('ghbdtn');await evaluate(`pmHook=()=>{const v=pmViews.prose;v.dispatch(v.state.tr.insertText('!'));return 'привет';}`);await type(' ');await evaluate('pmHook=null');await type('x');assert.equal(await text(),'ghbdtn!x');assert.equal(await evaluate('pmAdapter.status(pmViews.prose)'),'disabled');advanced++;
 for(const mode of ['filter','throw','rewrite']){
  stage=mode==='filter'?'filtered-transaction':mode==='throw'?'thrown-dispatch':'unexpected-app-edit';await reset();await type('ghbdtn');await evaluate(`pmModes.prose=${JSON.stringify(mode)}`);await type(' ');await type('x');assert.equal(await text(),mode==='rewrite'?'ghbdtn!x':'ghbdtn x');assert.equal(await evaluate('pmAdapter.status(pmViews.prose)'),'disabled');advanced++;
 }
 stage='revoked-registration';await reset();await type('ghbdtn');await evaluate(`pmHook=()=>{pmAdapter.unregister(pmViews.prose);return 'привет';}`);await type(' ');await evaluate('pmHook=null');await type('x');assert.equal(await text(),'ghbdtn x');assert.equal(await evaluate('pmAdapter.status(pmViews.prose)'),'unregistered');advanced++;
 stage='save-roundtrip';await reset();await type('ghbdtn x');assert.equal(await evaluate(`(()=>{const v=pmViews.prose,json=JSON.stringify(v.state.doc.toJSON());v.destroy();pmViews.prose=pmMakeView('prose',pmSchema.nodeFromJSON(JSON.parse(json)));pmViews.prose.focus();return JSON.stringify(pmViews.prose.state.doc.toJSON())===json;})()`),true);assert.equal(await text(),'привет x');advanced++;
 stage='destroy';assert.equal(await evaluate(`(()=>{const v=pmViews.unknown;v.destroy();return pmAdapter.status(v)==='unregistered';})()`),true);advanced++;
 stage='composition-gate';await reset();await type('ghbdtn');assert.equal(await evaluate(`(()=>{const v=pmViews.prose,before=pmDecisions;v.dom.dispatchEvent(new CompositionEvent('compositionstart',{bubbles:true}));const composing=v.composing;const handled=v.someProp('handleTextInput',f=>f(v,v.state.selection.from,v.state.selection.to,' ',()=>v.state.tr));v.dom.dispatchEvent(new CompositionEvent('compositionend',{bubbles:true}));return composing&&!handled&&pmDecisions===before;})()`),true);advanced++;
 // Recreate the view after synthetic composition instead of touching PM private flags.
 await evaluate(`pmViews.prose.destroy();pmViews.prose=pmMakeView('prose',pmMakeDoc());pmViews.prose.focus();`);
 stage='read-only';await reset();await type('ghbdtn');assert.equal(await evaluate(`(()=>{const v=pmViews.prose,before=pmDecisions;v.setProps({editable:()=>false});const handled=v.someProp('handleTextInput',f=>f(v,v.state.selection.from,v.state.selection.to,' ',()=>v.state.tr));return !handled&&pmDecisions===before;})()`),true);await evaluate('pmViews.prose.setProps({editable:()=>true})');advanced++;
 stage='dispose';await reset();const ready=await evaluate('pmSession.stats()');await evaluate('pmSession.dispose();pmSession.dispose()');await type('ghbdtn ');assert.equal(await text(),'ghbdtn ');assert.equal(await evaluate('pmAdapter.status(pmViews.prose)'),'disposed');advanced++;
 assert.equal(advanced,cfg.protocol.advanced.length);assert.equal(await evaluate('pmNetworkAttempts'),0);assert.equal(await evaluate('pmExecCalls'),0);
 return{passed:true,browser:product,editor:'ProseMirror',cases,advancedChecks:advanced,hashAndInitMs:Math.round(init.initMs*10)/10,heapBytes:ready.heapBytes,modelBytes:ready.modelBytes,stemBytes:ready.stemBytes,engineCount:ready.engineCount,
  networkAttempts:0,execCommandCalls:0,nativeHistoryPassed:true,externalApplicationSupport:false,packages:provenance.packages.map(p=>({name:p.name,version:p.version}))};
}
let report;try{report=await main();}catch{report={passed:false,error:'prosemirror-test-failed',stage};process.exitCode=1;}
finally{if(browser)try{await browser.close();}catch{report={passed:false,error:'prosemirror-close-failed',stage};process.exitCode=1;}}
process.stdout.write(JSON.stringify(report)+'\n');
