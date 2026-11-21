// SPDX-License-Identifier: MIT
// Privacy/lifecycle guards on own stubs. Actual PM behavior is tested in Chromium.
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const {createAdapter}=createRequire(import.meta.url)('../../browser/prosemirror.js');
function fixture(){
 const seen={text:0,decide:0,classify:0,dispatch:0},doc={};
 class PluginKey{constructor(){this.key='test-plugin';}getState(s){return s.pluginState;}}
 class Plugin{constructor(spec){this.spec=spec;this.key=spec.key.key;}}
 const historyPlugin={};let code=false,marks=[],credential=false,composition=false,editable=true;
 const owner={role:'prose'},flags={};
 function makeDoc(text){
  const node={type:{spec:{code:false}},marks:[],isInline:true,isText:true};
  const parent={isTextblock:true,content:{size:text.length},type:{spec:{}},
   textBetween(a,b){seen.text++;return text.slice(a,b);}};
  return{text,eq(other){return other.text===text;},resolve(from){return{parent,parentOffset:from-1,depth:1,start:()=>1,node:()=>({type:{spec:{code}}})};},
   slice(from,to){return{openStart:0,openEnd:0,content:{forEach(cb){cb({isText:true,nodeSize:to-from,marks:[]});}}};},
   nodesBetween(a,b,callback){callback({...node,marks});}};
 }
 function makeState(text='ghbdtn',selection={empty:true,from:7,to:7}){
  selection.$from={marks:()=>[]};
  const state={doc:makeDoc(text),selection,schema:{text:(text,marks)=>({text,marks})},plugins:[],pluginState:{suppressSpace:false}};
  Object.defineProperty(state,'tr',{get(){const meta=new Map();return{doc:state.doc,selection:state.selection,
   replaceWith(from,to,nodes){return this.insertText(nodes.map(n=>n.text).join(''),from,to);},setStoredMarks(){return this;},
   insertText(value,from,to=from){this.doc=makeDoc(this.doc.text.slice(0,from-1)+value+this.doc.text.slice(to-1));this.selection={empty:true,from:from+value.length,to:from+value.length};return this;},
   setMeta(key,value){meta.set(key.key||key,value);return this;},getMeta(key){return meta.get(key.key||key);}};}});
  return state;
 }
 let hook=null,mode='normal';
 const view={dom:{ownerDocument:doc,isConnected:true,spellcheck:true,getAttribute:()=>'',closest(selector){return selector==='form'&&credential?{querySelector:()=>({})}:null;}},
  state:makeState(),get composing(){return composition;},get editable(){return editable;},hasFocus:()=>true,
  dispatch(tr){seen.dispatch++;if(mode==='throw')throw Error('private owner detail');if(mode==='filter')return;
   this.state=makeState(mode==='unexpected'?'ghbdtn!':tr.doc.text,tr.selection);this.state.plugins=[historyPlugin,adapter.plugin];
   if(mode==='throw-after')throw Error('private owner detail');}
 };
 const adapter=createAdapter({document:doc,historyPlugin,modules:{Plugin,PluginKey,closeHistory:tr=>tr,isHistoryTransaction:tr=>tr.history===true},
  classify(){seen.classify++;return owner.role;},decide(token){seen.decide++;return hook?hook(token):'привет';}});
 view.state.plugins=[historyPlugin,adapter.plugin];
 const register=()=>adapter.register(view,{role:'prose',layout:'us'});
 const input=()=>adapter.plugin.spec.props.handleTextInput(view,view.state.selection.from,view.state.selection.to,' ');
 return{adapter,view,seen,owner,register,input,setCode:()=>code=true,setMarks:m=>marks=m,setCredential:()=>credential=true,
  setComposing:()=>composition=true,setReadOnly:()=>editable=false,setHook:f=>hook=f,setMode:m=>mode=m,makeState};
}
test('unregistered/unknown/async role never reads text or asks detector',()=>{
 const f=fixture();assert.equal(f.input(),false);assert.equal(f.seen.text,0);
 f.register();for(const role of ['unknown','secret','command',Promise.resolve('prose')]){f.owner.role=role;assert.equal(f.input(),false);}
 assert.equal(f.seen.text,0);assert.equal(f.seen.decide,0);
});
test('credential metadata, code blocks, inline-code/link marks guard before text extraction',()=>{
 for(const protect of [f=>f.setCredential(),f=>f.setCode(),f=>f.setMarks([{type:{spec:{code:true},name:'code'}}]),f=>f.setMarks([{type:{spec:{},name:'link'}}])]){
  const f=fixture();f.register();protect(f);assert.equal(f.input(),false);assert.equal(f.seen.text,0);assert.equal(f.seen.decide,0);
 }
});
test('composition, read-only and selection do not ask detector',()=>{
 for(const protect of [f=>f.setComposing(),f=>f.setReadOnly(),f=>f.view.state.selection={empty:false,from:1,to:4}]){
  const f=fixture();f.register();protect(f);assert.equal(f.input(),false);assert.equal(f.seen.text,0);assert.equal(f.seen.decide,0);
 }
});
test('normal correction is one owned transaction and does not use DOM editing',()=>{
 const f=fixture();f.register();assert.equal(f.input(),true);assert.equal(f.view.state.doc.text,'привет ');assert.equal(f.seen.dispatch,1);assert.equal(f.seen.decide,1);
});
test('revoking registration inside detector prevents the pending correction',()=>{
 const f=fixture();f.register();f.setHook(()=>{f.adapter.unregister(f.view);return'привет';});
 assert.equal(f.input(),false);assert.equal(f.seen.dispatch,0);assert.equal(f.view.state.doc.text,'ghbdtn');assert.equal(f.adapter.status(f.view),'unregistered');
});
test('stale state disables without applying a replacement to the new selection',()=>{
 const f=fixture();f.register();f.setHook(()=>{f.view.state=f.makeState('ghbdtn',{empty:true,from:1,to:1});return'привет';});
 assert.equal(f.input(),true);assert.equal(f.seen.dispatch,0);assert.equal(f.adapter.status(f.view),'disabled');assert.equal(f.input(),false);assert.equal(f.seen.decide,1);
});
test('Promise or thrown detector disables and allows proven unchanged ordinary input',()=>{
 for(const hook of [()=>Promise.resolve('привет'),()=>{throw Error('sensitive detail');}]){
  const f=fixture();f.register();f.setHook(hook);assert.equal(f.input(),false);assert.equal(f.seen.dispatch,0);assert.equal(f.adapter.status(f.view),'disabled');
 }
});
test('owner no-op/throw/unexpected edit is preserved with no guessed rollback',()=>{
 for(const mode of ['filter','throw','unexpected','throw-after']){
  const f=fixture();f.register();f.setMode(mode);const consumed=f.input();
  assert.equal(consumed,mode==='unexpected'||mode==='throw-after');
  assert.equal(f.view.state.doc.text,mode==='unexpected'?'ghbdtn!':mode==='throw-after'?'привет ':'ghbdtn');
  assert.equal(f.adapter.status(f.view),'disabled');assert.equal(f.seen.dispatch,1);
 }
});
test('history suppression, view destroy and dispose leave ordinary editing to owner',()=>{
 const f=fixture();f.register();f.view.state.pluginState={suppressSpace:true};assert.equal(f.input(),false);assert.equal(f.seen.text,0);
 f.view.isDestroyed=true;assert.equal(f.adapter.status(f.view),'unregistered');
 f.view.isDestroyed=false;f.register();f.adapter.dispose();f.adapter.dispose();assert.equal(f.input(),false);assert.equal(f.seen.decide,0);assert.equal(f.adapter.status(f.view),'disposed');assert.throws(f.register,TypeError);
});

test('pending protected marks prevent prefix extraction even on an unmarked word',()=>{
 const f=fixture();f.register();f.view.state.storedMarks=[{type:{name:'code',spec:{code:true}}}];
 assert.equal(f.input(),false);assert.equal(f.seen.text,0);assert.equal(f.seen.decide,0);
});
