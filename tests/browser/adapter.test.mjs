// Project license pending; see README.md
import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
const require=createRequire(import.meta.url);
const {createAdapter}=require('../../browser/adapter.js');
execFileSync(fileURLToPath(new URL('../../bin/build-browser-fixture',import.meta.url)),{stdio:'ignore'});
const {convert}=require('../../build/browser-fixture/converter.js');
function setup(){
  const listeners=new Map();let reads=0,queries=0;
  const doc={activeElement:null};
  const field={ownerDocument:doc,tagName:'TEXTAREA',isConnected:true,spellcheck:true,
    readOnly:false,disabled:false,selectionStart:6,selectionEnd:6,
    getAttribute:()=>null,closest:()=>null,form:null,
    addEventListener:(name,fn)=>listeners.set(name,fn),removeEventListener:name=>listeners.delete(name),
    get value(){reads++;return 'ghbdtn';}};
  doc.activeElement=field;
  const adapter=createAdapter({document:doc,decide:()=>{queries++;return null;}});
  const fire=()=>listeners.get('beforeinput')?.({target:field,isTrusted:true,cancelable:true,
    defaultPrevented:false,inputType:'insertText',data:' ',isComposing:false});
  return {doc,field,adapter,fire,listeners,counts:()=>({reads,queries})};
}
for(const guard of ['unregistered','readonly','disabled','inactive','disconnected','code','password','credential','unknown-autocomplete']){
  test('no text access: '+guard,()=>{
    const x=setup();if(guard!=='unregistered')x.adapter.register(x.field,{role:'prose',layout:'us'});
    if(guard==='readonly')x.field.readOnly=true;
    if(guard==='disabled')x.field.disabled=true;
    if(guard==='inactive')x.doc.activeElement=null;
    if(guard==='disconnected')x.field.isConnected=false;
    if(guard==='code')x.field.spellcheck=false;
    if(guard==='password')x.field.form={elements:[{tagName:'INPUT',type:'password'}]};
    if(guard==='credential')x.field.getAttribute=()=> 'one-time-code';
    if(guard==='unknown-autocomplete')x.field.getAttribute=()=> 'custom-token';
    x.fire();assert.deepEqual(x.counts(),{reads:0,queries:0});
  });
}
test('dispose releases field listeners',()=>{const x=setup();x.adapter.register(x.field,{role:'prose',layout:'us'});
  x.adapter.dispose();assert.equal(x.listeners.size,0);assert.equal(x.adapter.status(x.field),'unregistered');});
test('reject unsupported editor and layout',()=>{const x=setup();
  assert.throws(()=>x.adapter.register(x.field,{role:'prose',layout:'ru'}));
  x.field.tagName='INPUT';assert.throws(()=>x.adapter.register(x.field,{role:'prose',layout:'us'}));});
test('generated map respects shifts, marks and untouched Unicode',()=>{
  assert.equal(convert('Ghbdtn, vbh!', 'us-ru'),'Приветб мир!');
  assert.equal(convert('Приветб мир!', 'ru-us'),'Ghbdtn, vbh!');
  assert.equal(convert('a\u0301a\u{1D165}😀', 'us-ru'),'a\u0301a\u{1D165}😀');
  assert.equal(convert('hello','invalid'),'hello');
});
test('all printable ASCII symbols roundtrip',()=>{
  const text=Array.from({length:94},(_,i)=>String.fromCharCode(i+33)).join('');
  assert.equal(convert(convert(text,'us-ru'),'ru-us'),text);
});
