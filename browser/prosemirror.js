// SPDX-License-Identifier: MIT
// Owner-integrated ProseMirror transactions. No DOM text editing or global input.
(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;else root.RaskladProseMirror=api;})(globalThis,function(){
 'use strict';
 function createAdapter({document:doc,decide,modules,historyPlugin,classify}={}){
  const Plugin=modules?.Plugin,PluginKey=modules?.PluginKey;
  const closeHistory=modules?.closeHistory,isHistoryTransaction=modules?.isHistoryTransaction;
  if(!doc||typeof decide!=='function'||typeof classify!=='function'||!historyPlugin||
   [Plugin,PluginKey,closeHistory,isHistoryTransaction].some(fn=>typeof fn!=='function'))throw new TypeError('invalid editor adapter');
  const key=new PluginKey('rasklad-layout');
  let closed=false,entries=new WeakMap();
  const disable=r=>{if(r)r.disabled=true;};
  const protectedMark=m=>m.type.spec.code||m.type.name==='link';
  // Public state identity includes selection-only changes, not merely document edits.
  const changed=(view,state)=>view.state!==state;
  function metadataSafe(view,record){
   const dom=view.dom;
   if(closed||!record||entries.get(view)!==record||record.disabled||!dom?.isConnected||dom.ownerDocument!==doc||
    view.isDestroyed===true||view.editable!==true||view.composing||!view.hasFocus()||
    !view.state.plugins.includes(historyPlugin)||!view.state.plugins.includes(plugin))return false;
   const autocomplete=(dom.getAttribute('autocomplete')||'').trim().toLowerCase();
   if(dom.spellcheck===false||!['','on','off'].includes(autocomplete)||dom.closest('pre,code,[inert],[aria-hidden="true"]'))return false;
   const form=dom.closest('form');
   return !form||!form.querySelector('input[type="password" i],input[autocomplete~="current-password" i],input[autocomplete~="new-password" i],input[autocomplete~="one-time-code" i]');
  }
  function authorizedWindow(view,state,from,to,record){
   if(changed(view,state)||!metadataSafe(view,record)||!state.selection.empty||from!==to||
    state.selection.from!==from||state.selection.to!==to||!Number.isSafeInteger(from))return null;
   if(state.storedMarks?.some(protectedMark))return null;
   const at=state.doc.resolve(from),parent=at.parent;
   if(!parent.isTextblock)return null;
   for(let depth=0;depth<=at.depth;depth++)if(at.node(depth).type.spec.code)return null;
   const start=at.start(),end=start+parent.content.size;
   const scanFrom=Math.max(start,from-33),scanTo=Math.min(end,from+1);
   // The owner supplies semantic roles from its schema/range policy. No text is
   // materialized until both that policy and the independent code/link guard pass.
   if(classify(state,scanFrom,scanTo)!=='prose'||changed(view,state))return null;
   let protectedSpan=false;
   state.doc.nodesBetween(scanFrom,scanTo,node=>{
    if(node.type.spec.code||node.marks.some(protectedMark)||
     (node.isInline&&!node.isText)){protectedSpan=true;return false;}
    return true;
   });
   if(protectedSpan||changed(view,state))return null;
   return{at,parent};
  }
  function input(view,from,to,text){
   const record=entries.get(view);
   if(text!==' '||from!==to||!record||record.busy||record.disabled||closed)return false;
   const state=view.state;
   if(key.getState(state)?.suppressSpace)return false;
   record.busy=true;
   try{
    const window=authorizedWindow(view,state,from,to,record);
    if(!window){if(changed(view,state)){disable(record);return true;}return false;}
    const {at,parent}=window,offset=at.parentOffset;
    if(offset<parent.content.size&&!/^\s$/u.test(parent.textBetween(offset,offset+1,'','\ufffc')))return false;
    const prefix=parent.textBetween(Math.max(0,offset-33),offset,'','\ufffc');
    const match=/(?:^|\s)([A-Za-z]{4,32})$/u.exec(prefix);
    if(!match||(offset>32&&prefix.length===match[1].length))return false;
    const token=match[1],wordFrom=from-token.length;
    let replacement;
    try{replacement=decide(token);}catch{disable(record);return changed(view,state);}
    // Even a no-action response cannot authorize the framework's stale default
    // transaction after an application-side state change during detection.
    if(changed(view,state)){disable(record);return true;}
    if(replacement==null)return false;
    if(typeof replacement!=='string'||replacement.length!==token.length||!/^[А-Яа-яЁё]{4,32}$/u.test(replacement)){
     disable(record);return false;
    }
    if(!authorizedWindow(view,state,from,to,record)||changed(view,state)){
     disable(record);return changed(view,state);
    }
    // Preserve every original text run's marks, including mixed and non-inclusive
    // annotations. Space gets the marks native insertion would have used at the
    // original cursor; toggling a mark must never restyle the preceding word.
    const slice=state.doc.slice(wordFrom,from),nodes=[];
    if(slice.openStart||slice.openEnd)return false;
    let consumed=0,onlyText=true;
    slice.content.forEach(node=>{
     if(!node.isText){onlyText=false;return;}
     const length=node.nodeSize;
     nodes.push(state.schema.text(replacement.slice(consumed,consumed+length),node.marks));
     consumed+=length;
    });
    if(!onlyText||consumed!==replacement.length)return false;
    const spaceMarks=state.storedMarks||state.selection.$from.marks();
    const transaction=closeHistory(state.tr.replaceWith(wordFrom,from,nodes)
     .setStoredMarks(spaceMarks).insertText(' ',from)).setMeta(key,{auto:true});
    let threw=false;
    try{view.dispatch(transaction);}catch{threw=true;}
    const current=view.state;
    if(current.doc.eq(transaction.doc)&&current.selection.empty&&
     current.selection.from===transaction.selection.from&&current.selection.to===transaction.selection.to){
     if(threw)disable(record);
     return true;
    }
    // Only exact state identity proves no-op: ordinary Space is then safe.
    // Unexpected owner edits are preserved and consumed, never guessed/replayed.
    disable(record);return current!==state;
   }catch{
    disable(record);return changed(view,state);
   }finally{record.busy=false;}
  }
  const plugin=new Plugin({
   key,
   state:{init:()=>({suppressSpace:false}),apply(transaction,previous){
    if(isHistoryTransaction(transaction))return{suppressSpace:true};
    return transaction.docChanged?{suppressSpace:false}:previous;
   }},
   props:{handleTextInput:input},
   appendTransaction(transactions,oldState,newState){
    if(closed||!transactions.some(tr=>tr.getMeta(key)?.auto)||transactions.some(tr=>tr.getMeta(key)?.boundary))return null;
    // A separate zero-step boundary keeps subsequent typing outside correction's
    // undo group, without adding another undoable document change.
    return closeHistory(newState.tr).setMeta('addToHistory',false).setMeta(key,{boundary:true});
   }
  });
  function register(view,options){
   if(closed||view?.isDestroyed===true||options?.role!=='prose'||options?.layout!=='us'||view?.dom?.ownerDocument!==doc||
    !view.state.plugins.includes(plugin)||!view.state.plugins.includes(historyPlugin)||entries.has(view))
    throw new TypeError('explicit owned prose editor with history required');
   entries.set(view,{busy:false,disabled:false});
  }
  function unregister(view){disable(entries.get(view));entries.delete(view);}
  return Object.freeze({plugin,register,unregister,
   dispose(){closed=true;entries=new WeakMap();},
   status(view){if(closed)return'disposed';if(view?.isDestroyed===true)unregister(view);return entries.has(view)?entries.get(view).disabled?'disabled':'registered':'unregistered';}});
 }
 return Object.freeze({createAdapter});
});
