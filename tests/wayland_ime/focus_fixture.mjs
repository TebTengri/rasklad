// SPDX-License-Identifier: MIT
// Owned DOM mock for oracle/telemetry unit tests; not a browser or Wayland proof.
import vm from 'node:vm';
import {setMaxListeners} from 'node:events';
export function fixture(mode='normal') {
  const allFields=[];
  const timers=new Set();
  const context=vm.createContext({AbortController});
  let composing=null;
  let bypass=false;
  let injected=false;
  const fields={};
  function field(id) {
    const listeners=new Map();
    const signals=new Map();
    const element={id,value:'',
      addEventListener(type, fn, options) {
        if(options?.signal?.aborted)return;
        if(options?.signal)setMaxListeners(16,options.signal);
        if(!listeners.has(type))listeners.set(type,new Set());listeners.get(type).add(fn);
        if(options?.signal){const remove=()=>element.removeEventListener(type,fn);signals.set(fn,{signal:options.signal,remove});options.signal.addEventListener('abort',remove,{once:true});}
      },
      removeEventListener(type, fn) {listeners.get(type)?.delete(fn);const entry=signals.get(fn);if(entry){entry.signal.removeEventListener('abort',entry.remove);signals.delete(fn);}},
      focus() {
        const old=context.document.activeElement;
        if(old===element)return;
        if(old===composing){composing=null;emit(old,'compositionend');}
        context.document.activeElement=element;
      },
      listenerCount() {return [...listeners.values()].reduce((n,set)=>n+set.size,0);},
      listeners,
    };
    allFields.push(element);
    return element;
  }
  for(const id of ['prose','other','secret'])fields[id]=field(id);
  const doc=field('document');
  context.document={getElementById:id=>fields[id]||null,activeElement:fields.prose,
    addEventListener:doc.addEventListener,removeEventListener:doc.removeEventListener};
  context.window=context;
  context.imeEvents=[];
  context.setTimeout=(fn,delay)=>{
    const timer=setTimeout(()=>{timers.delete(timer);fn();},delay);timers.add(timer);return timer;
  };
  context.clearTimeout=timer=>{timers.delete(timer);clearTimeout(timer);};
  function emit(element,type,details={}) {
    if(type==='compositionend'&&['wrong_target_end','wrong_target_untrusted_end'].includes(mode))element=fields.other;
    const event={target:element,isTrusted:mode!=='untrusted_events'&&!(type==='compositionend'&&['native_untrusted_end','wrong_target_untrusted_end'].includes(mode)),type,...details};
    for(const name of ['key','data'])Object.defineProperty(event,name,{get(){throw Error('forbidden event payload access');}});
    context.imeEvents.push({type,inputType:'',length:0});
    for(const fn of [...(doc.listeners.get(type)||[])])fn(event);
    for(const fn of [...(element.listeners.get(type)||[])])fn(event);
  }
  async function ev(expression) {
    const result=await vm.runInContext(expression,context,{timeout:250});
    return mode==='unexpected_snapshot_code'&&typeof result==='number'&&result>=100?200:result;
  }
  async function reset(id='prose') {
    composing=null;
    for(const element of Object.values(fields))element.value='';
    context.document.activeElement=fields[id];
    context.imeEvents=[];
  }
  async function type(command) {
    const active=context.document.activeElement;
    if(command==='word') {
      active.value='ghbdtn';
      if(!bypass){composing=active;emit(active,'compositionstart');}
    } else if(command==='escape') {
      if(mode!=='escape_ignored'&&composing){const old=composing;composing=null;bypass=true;if(mode!=='escape_end_missing')emit(old,'compositionend');}
    } else if(command==='space') {
      if(mode==='throw_on_space')throw Error('PRIVATE_SENTINEL_MUST_NOT_ESCAPE');
      emit(active,'keydown',{code:'Space',keyCode:32});
      emit(active,'beforeinput',{inputType:'insertText'});
      if(bypass){bypass=false;active.value+=' ';}
      else if(composing) {
        const old=composing;composing=null;old.value='привет ';
        if(mode!=='hook_event_missing')emit(old,'compositionend');
        if(mode==='double_hook')emit(old,'compositionend');
      } else active.value+=mode==='cleanup_payload'&&context.__raskladFocus===undefined?'ghbdtn ':' ';
      emit(active,'input',{inputType:'insertText'});
      emit(context.document.activeElement,'keyup',{code:'Space',keyCode:32});
      if(mode==='state_deleted')delete context.__raskladFocus;
      if(mode==='state_cloned'&&context.__raskladFocus)context.__raskladFocus={...context.__raskladFocus};
      if(mode==='controller_aborted')context.__raskladFocusCtl?.abort();
      if(mode==='node_replaced'&&!injected){fields.other=field('other');injected=true;}
      if(mode==='word_lost')fields.prose.value='';
    } else throw Error('unsupported fixed command');
  }
  return {ev,type,reset,allFields,timers,emit:(type,details={})=>emit(context.document.activeElement,type,details),
    emitUnknown:(type,details={})=>{const unknown=field('unknown');Object.defineProperty(unknown,'id',{get(){throw Error('unknown target id must not be read');}});emit(unknown,type,details);}};
}

