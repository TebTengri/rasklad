// SPDX-License-Identifier: MIT
// CDP only observes/configures our fixture. All typing uses private XTest.
import {spawn,execFileSync} from 'node:child_process';
import {readFileSync,readlinkSync} from 'node:fs';
import {createHash} from 'node:crypto';
const nativeWayland=process.env.RASKLAD_NATIVE_WAYLAND_LAB==='1';
if(process.env.RASKLAD_CHROMIUM_IME_LAB!=='1'||process.env.HOME!=='/home/lab'||
 (nativeWayland?(process.env.DISPLAY||process.env.WAYLAND_DISPLAY!=='wayland-lab'):process.env.DISPLAY!==':77'))throw Error('not private lab');
let vk=null,vkExited=null,vkAlive=false,vkBuffer='',vkWaiters=[];
function nextVkLine(timeout=4000){return new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('private virtual-keyboard timeout')),timeout);vkWaiters.push(x=>{clearTimeout(timer);resolve(x);});});}
if(nativeWayland){
 vk=spawn('/lab/rasklad-wayland-keys',[],{stdio:['pipe','pipe','ignore']});vkAlive=true;vkExited=new Promise(r=>vk.once('exit',()=>{vkAlive=false;r();}));vk.stdout.setEncoding('utf8');
 const ready=nextVkLine();
 vk.stdout.on('data',data=>{vkBuffer+=data;let at;while((at=vkBuffer.indexOf('\n'))>=0){const line=vkBuffer.slice(0,at);vkBuffer=vkBuffer.slice(at+1);vkWaiters.shift()?.(line);}});
 if(await ready!=='{"ready":true}')throw Error('private virtual-keyboard failed');
}

const child=spawn('/usr/lib/chromium/chromium',[
 ...(nativeWayland?['--ozone-platform=wayland','--enable-wayland-ime','--wayland-text-input-version=3']:['--ozone-platform=x11','--gtk-version=3']),'--disable-gpu','--disable-dev-shm-usage',
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
const report={schema:1,application:nativeWayland?'installed Chromium native Wayland IM':'installed Chromium via X11 GTK IM',syntheticInputOnly:true,slackSupport:false,cdpTyping:false};
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
 async function type(name){if(nativeWayland){const ack=nextVkLine(5000);vk.stdin.write(name+'\n');if(await ack!=='{"ok":true}')throw Error('private virtual-keyboard command failed');}else execFileSync('/usr/bin/python3',['-I','/lab/keys.py',name,String(child.pid)],{stdio:['ignore','ignore','pipe'],timeout:5000});await new Promise(r=>setTimeout(r,200));}
 await reset();phase='word';await type('word');
 report.preeditObserved=await ev(`imeEvents.some(x=>x.type==='compositionstart')&&imeEvents.some(x=>x.type==='compositionupdate')`);
 report.beforeSpaceDomLength=await ev(`prose.value.length`);
 report.compositionActiveBeforeSpace=await ev(`imeEvents.filter(x=>x.type==='compositionstart').length>imeEvents.filter(x=>x.type==='compositionend').length`);
 phase='space';await type('space');
 report.correctedOnce=await ev(`prose.value==='привет '`);
 report.compositionEnded=await ev(`imeEvents.some(x=>x.type==='compositionend')`);
 if(process.env.RASKLAD_FOCUS_STRESS_LAB==='1')report.focusEventControl=await ev(`(()=>{
  const count=(type,trust)=>imeEvents.filter(e=>e.type===type&&e.targetIndex===0&&(trust===null||e.trusted===trust)).length;
  return {trustedSourceStarts:count('compositionstart',true),sourceEnds:count('compositionend',null),
   trustedSourceEnds:count('compositionend',true),untrustedSourceEnds:count('compositionend',false),
   totalStarts:imeEvents.filter(e=>e.type==='compositionstart').length,totalEnds:imeEvents.filter(e=>e.type==='compositionend').length};})()`);
 if(process.env.RASKLAD_PROFILE_REJECT_TEST==='1'){
  report.profileRejectedNoInterception=await ev(`prose.value==='ghbdtn '&&!imeEvents.some(x=>x.type==='compositionstart')`);
  report.rejectionTest=true;report.passed=report.profileRejectedNoInterception;report.checks=1;
 }else{
 phase='nextword';await type('xspace');report.nextWordPreserved=await ev(`prose.value==='привет x '`);
 phase='undo';const beforeUndo=await ev(`prose.value`);await type('undo');report.nativeUndoChangesDocument=await ev(`prose.value!==${JSON.stringify(beforeUndo)}`);report.nativeUndoRestoresPreviousCommit=await ev(`prose.value==='привет '`);
 phase='backspace_escape';await reset();await type('word');await type('backspace');report.backspaceEditsComposition=await ev(`prose.value==='ghbdt'`);await type('escape');report.escapePreservesOriginal=await ev(`prose.value==='ghbdt'`);
 phase='mixed';await reset();await type('mixed');report.mixedTokenPreserved=await ev(`prose.value==='1ghbdtn '`);
 phase='long';await reset();await type('long');report.longTokenPreserved=await ev(`prose.value==='a'.repeat(33)+'ghbdtn '`);
 phase='password';await reset('secret');await type('word');await type('space');report.passwordPassthrough=await ev(`secret.value==='ghbdtn '&&!imeEvents.some(x=>x.type==='compositionstart')`);
 phase='focus';await reset();await type('word');await ev(`other.focus();imeEvents=[]`);await new Promise(r=>setTimeout(r,120));await type('space');report.focusDoesNotLeak=await ev(`other.value===' '&&!imeEvents.some(x=>x.type==='compositionstart')`);report.focusOriginalWordPreserved=await ev(`prose.value==='ghbdtn'`);
 if(process.env.RASKLAD_REVIEWED_PROFILE_LAB==='1'){
  phase='profile_second_word';await reset();await type('sample2');report.secondWordCorrected=await ev(`prose.value==='пример '`);
  phase='profile_known_english';await reset();await type('hello');report.knownEnglishPreserved=await ev(`prose.value==='hello '`);
  report.profileVariant=true;
 }
 const required=['preeditObserved','compositionActiveBeforeSpace','correctedOnce','compositionEnded','nextWordPreserved','nativeUndoRestoresPreviousCommit','backspaceEditsComposition','escapePreservesOriginal','mixedTokenPreserved','longTokenPreserved','passwordPassthrough','focusDoesNotLeak','focusOriginalWordPreserved'];
 if(report.profileVariant)required.push('secondWordCorrected','knownEnglishPreserved');
 if(process.env.RASKLAD_FOCUS_STRESS_LAB==='1'){
  if(!nativeWayland)throw Error('native focus suite required');
  phase='focus_stress';
  const review=JSON.parse(readFileSync('/lab/focus-source-review.json','utf8'));
  const sourceNames=['driver.mjs','adversarial-focus.mjs','focus-policy.mjs','space-telemetry.mjs','fixture.html'];
  const sourcesReviewed=review.schema===1&&review.reviewedNoScriptEventGeneration===true&&sourceNames.every(name=>{
   const bytes=readFileSync('/lab/'+name);return bytes.length<=40000&&createHash('sha256').update(bytes).digest('hex')===review.sourceSha256?.[name];
  });
  if(!sourcesReviewed)throw Error('focus source provenance refused');
  const {selectEndEventPolicy}=await import('file:///lab/focus-policy.mjs');
  const endEventPolicy=selectEndEventPolicy({browserVersion:report.browserVersion,sourcesReviewed,control:report.focusEventControl,correctedOnce:report.correctedOnce});
  if(endEventPolicy===null)throw Error('focus event provenance refused');
  report.focusSourceReviewPassed=sourcesReviewed;
  const {runFocusScenarios}=await import('file:///lab/adversarial-focus.mjs');
  const diagnostics=[];
  async function focusEv(expression){
   const result=await ev(expression);
   if((result===false||(Number.isInteger(result)&&result<100))&&diagnostics.length<8){
    try{
     const observation=await ev(`(()=>{const s=window.__raskladFocus,c=window.__raskladFocusCtl;
      const n=x=>Number.isInteger(x)&&x>=0&&x<=1000?x:-1;
      const ids=['prose','other','secret'];const active=ids.indexOf(document.activeElement?.id);
      return {caseIndex:['word_focus_secret_space','word_other_back_prose_space','word_escape_other_space','word_space_compositionend_hook_other'].indexOf(s?.token),
       statePresent:!!s,stateOwned:!!s&&typeof c?.owns==='function'&&c.owns(s)===true,activeFieldIndex:active,
       starts:ids.map(id=>n(s?.starts?.[id])),ends:ids.map(id=>n(s?.ends?.[id])),endMark:n(s?.endMark),
       documentCompositionStarts:Math.min(1000,imeEvents.filter(e=>e.type==='compositionstart').length),
       documentCompositionEnds:Math.min(1000,imeEvents.filter(e=>e.type==='compositionend').length),
       documentEndTargets:[-1,0,1,2].map(i=>Math.min(1000,imeEvents.filter(e=>e.type==='compositionend'&&e.targetIndex===i).length)),
       documentTrustedEnds:Math.min(1000,imeEvents.filter(e=>e.type==='compositionend'&&e.trusted===true).length),
       fieldValueClasses:ids.map(id=>['',' ','ghbdtn','ghbdtn ','привет','привет '].indexOf(document.getElementById(id).value)),
       proseIsOriginal:document.getElementById('prose').value==='ghbdtn',hookHits:n(s?.hits)};})()`);
     diagnostics.push({stage:Number.isInteger(result)?'final_snapshot':expression.startsWith('new Promise')?'source_end_barrier':'precondition_or_action',...observation});
    }catch{diagnostics.push({stage:'diagnostic_unavailable'});}
   }
   return result;
  }
  let spaceTelemetry=null;
  if(process.env.RASKLAD_SPACE_TELEMETRY_LAB==='1'){
   const {createSpaceTelemetry}=await import('file:///lab/space-telemetry.mjs');
   spaceTelemetry=createSpaceTelemetry({ev});
  }
  report.focusStress=await runFocusScenarios({ev:focusEv,type,reset,spaceTelemetry},endEventPolicy);
  report.focusStress.instrumentedSpaceTrial=spaceTelemetry!==null;
  report.focusStress.scope='DOM_intra_surface_only';
  report.focusStress.diagnostics=diagnostics;
  report.focusStressPassed=report.focusStress.passed===true;
  required.push('focusStressPassed');
 }
 phase='crash_recovery';await reset();await type('word');
 const daemonPid=Number(process.env.RASKLAD_OWN_FCITX_PID);
 if(!Number.isSafeInteger(daemonPid)||daemonPid<=1||readlinkSync(`/proc/${daemonPid}/exe`)!=='/usr/bin/fcitx5')throw Error('owned private daemon missing');
 const status=readFileSync(`/proc/${daemonPid}/status`,'utf8');
 report.daemonRssKiB=Number(status.match(/^VmRSS:\s+(\d+)/m)?.[1]||0);
 report.daemonHighWaterKiB=Number(status.match(/^VmHWM:\s+(\d+)/m)?.[1]||0);
 const stat=readFileSync(`/proc/${daemonPid}/stat`,'utf8').split(')')[1].trim().split(/\s+/);
 report.daemonCpuSeconds=(Number(stat[11])+Number(stat[12]))/Number(process.env.RASKLAD_CLK_TCK);
 process.kill(daemonPid,'SIGKILL');await new Promise(r=>setTimeout(r,500));
 await type('hello');
 report.postCrashInputContinues=await ev(`prose.value.endsWith('hello ')`);
 report.pendingWordSurvivedCrash=await ev(`prose.value==='ghbdtnhello '`);
 report.crashDocumentLength=await ev(`prose.value.length`);
 const owner=execFileSync('/usr/bin/gdbus',['call','--session','--dest','org.freedesktop.DBus','--object-path','/org/freedesktop/DBus','--method','org.freedesktop.DBus.NameHasOwner','org.fcitx.Fcitx5'],{encoding:'utf8',timeout:1000});
 report.daemonStayedStopped=owner==='(false,)\n';
 required.push('postCrashInputContinues','daemonStayedStopped');
 report.passed=required.every(k=>report[k]===true);report.checks=required.length;
 }
}catch(e){report.passed=false;report.failedPhase=phase;report.browserExit=lastExit;report.failure=String(e.message).startsWith('Command failed:')?'private_key_driver_failed':String(e.message).slice(0,80);}
finally{
 try{await Promise.race([call('Browser.close'),new Promise(r=>setTimeout(r,1000))]);}catch{}
 if(alive)child.kill('SIGTERM');
 await Promise.race([exited,new Promise(r=>setTimeout(r,1000))]);if(alive)child.kill('SIGKILL');if(vk){vk.kill('SIGTERM');await Promise.race([vkExited,new Promise(r=>setTimeout(r,500))]);if(vkAlive){vk.kill('SIGKILL');await vkExited;}}clearTimeout(deadline);
}
console.log(JSON.stringify(report));process.exitCode=report.passed?0:1;
