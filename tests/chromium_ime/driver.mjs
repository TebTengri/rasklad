// SPDX-License-Identifier: MIT
// CDP only observes/configures our fixture. All typing uses private XTest.
import {spawn,execFileSync} from 'node:child_process';
if(process.env.RASKLAD_CHROMIUM_IME_LAB!=='1'||process.env.DISPLAY!==':77'||process.env.HOME!=='/home/lab')throw Error('not private lab');
const child=spawn('/usr/lib/chromium/chromium',[
 '--ozone-platform=x11','--gtk-version=3','--disable-gpu','--disable-dev-shm-usage',
 '--disable-background-networking','--disable-component-update','--disable-sync','--no-first-run',
 '--no-default-browser-check','--password-store=basic','--user-data-dir=/tmp/browser-profile',
 '--remote-debugging-pipe','--window-size=900,700','about:blank'],
 {stdio:['ignore','ignore','pipe','pipe','pipe']});
let diagnostics='';child.stderr.on('data',d=>{diagnostics=(diagnostics+d).slice(-3500);});
let sequence=0,buffer='',alive=true,lastExit=null;const pending=new Map();
child.stdio[4].setEncoding('utf8');
const exited=new Promise(resolve=>child.once('exit',(code,signal)=>{alive=false;lastExit={code,signal};for(const p of pending.values())p.reject(Error('browser exited'));pending.clear();resolve({code,signal});}));
for(const pipe of [child.stdio[3],child.stdio[4]])pipe.on('error',()=>{});
child.stdio[4].on('data',data=>{buffer+=data;if(buffer.length>2_000_000)child.kill();let at;
 while((at=buffer.indexOf('\0'))>=0){let m;try{m=JSON.parse(buffer.slice(0,at));}catch{child.kill();return;}buffer=buffer.slice(at+1);if(m.id){const p=pending.get(m.id);pending.delete(m.id);if(m.error)p?.reject(Error('CDP failed'));else p?.resolve(m.result);}}});
function call(method,params={},sessionId){
 if(method.startsWith('Input.'))return Promise.reject(Error('CDP typing forbidden'));
 return new Promise((resolve,reject)=>{if(!alive)return reject(Error('browser closed'));const id=++sequence;pending.set(id,{resolve,reject});child.stdio[3].write(JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})})+'\0');});
}
const deadline=setTimeout(()=>child.kill('SIGKILL'),45000);
const report={schema:1,application:'installed Chromium via X11 GTK IM',syntheticInputOnly:true,slackSupport:false,cdpTyping:false};
let phase='start';
try{
 const version=await call('Browser.getVersion');report.browserVersion=version.product;
 const {targetId}=await call('Target.createTarget',{url:'file:///lab/fixture.html'});
 const {sessionId}=await call('Target.attachToTarget',{targetId,flatten:true});
 const c=(m,p)=>call(m,p,sessionId);
 async function ev(expression){const r=await c('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error('fixture evaluation');return r.result.value;}
 await c('Page.bringToFront');
 await ev(`new Promise((resolve,reject)=>{const end=Date.now()+4000;function ready(){if(document.getElementById('prose'))return resolve(true);if(Date.now()>end)return reject(Error('load'));setTimeout(ready,20);}ready();})`);
 async function reset(id='prose'){await ev(`document.querySelectorAll('textarea,input').forEach(x=>x.value='');document.getElementById(${JSON.stringify(id)}).focus();imeEvents=[]`);await new Promise(r=>setTimeout(r,150));}
 async function type(name){execFileSync('/usr/bin/python3',['-I','/lab/keys.py',name,String(child.pid)],{stdio:['ignore','ignore','pipe'],timeout:5000});await new Promise(r=>setTimeout(r,200));}
 await reset();phase='word';await type('word');
 report.preeditObserved=await ev(`imeEvents.some(x=>x.type==='compositionstart')&&imeEvents.some(x=>x.type==='compositionupdate')`);
 report.beforeSpaceDomLength=await ev(`prose.value.length`);
 report.compositionActiveBeforeSpace=await ev(`imeEvents.filter(x=>x.type==='compositionstart').length>imeEvents.filter(x=>x.type==='compositionend').length`);
 phase='space';await type('space');
 report.correctedOnce=await ev(`prose.value==='привет '`);
 report.compositionEnded=await ev(`imeEvents.some(x=>x.type==='compositionend')`);
 phase='nextword';await type('xspace');report.nextWordPreserved=await ev(`prose.value==='привет x '`);
 phase='undo';const beforeUndo=await ev(`prose.value`);await type('undo');report.nativeUndoChangesDocument=await ev(`prose.value!==${JSON.stringify(beforeUndo)}`);report.nativeUndoRestoresPreviousCommit=await ev(`prose.value==='привет '`);
 phase='mixed';await reset();await type('mixed');report.mixedTokenPreserved=await ev(`prose.value==='1ghbdtn '`);
 phase='long';await reset();await type('long');report.longTokenPreserved=await ev(`prose.value==='a'.repeat(33)+'ghbdtn '`);
 phase='password';await reset('secret');await type('word');await type('space');report.passwordPassthrough=await ev(`secret.value==='ghbdtn '&&!imeEvents.some(x=>x.type==='compositionstart')`);
 phase='focus';await reset();await type('word');await ev(`other.focus();imeEvents=[]`);await new Promise(r=>setTimeout(r,120));await type('space');report.focusDoesNotLeak=await ev(`other.value===' '&&!imeEvents.some(x=>x.type==='compositionstart')`);report.focusOriginalWordPreserved=await ev(`prose.value==='ghbdtn'`);
 const required=['preeditObserved','compositionActiveBeforeSpace','correctedOnce','compositionEnded','nextWordPreserved','nativeUndoRestoresPreviousCommit','mixedTokenPreserved','longTokenPreserved','passwordPassthrough','focusDoesNotLeak'];
 report.passed=required.every(k=>report[k]===true);report.checks=required.length;
}catch(e){report.passed=false;report.failedPhase=phase;report.browserExit=lastExit;report.failure=String(e.message).startsWith('Command failed:')?'private_key_driver_failed':String(e.message).slice(0,80);}
finally{
 try{await Promise.race([call('Browser.close'),new Promise(r=>setTimeout(r,1000))]);}catch{}
 if(alive)child.kill('SIGTERM');
 await Promise.race([exited,new Promise(r=>setTimeout(r,1000))]);if(alive)child.kill('SIGKILL');clearTimeout(deadline);
}
console.log(JSON.stringify(report));process.exitCode=report.passed?0:1;
