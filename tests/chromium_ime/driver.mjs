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
const supervised=process.env.RASKLAD_SLACK_SUPERVISED_LAB==='1';
// Supervised mode: only the Python supervisor owns the virtual keyboard.
if(nativeWayland&&!supervised){
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
 if(supervised){
  // Observer only: the Python Supervisor types through the helper it alone owns.
  if(!nativeWayland||process.env.RASKLAD_SLACK_GATE_LAB!=='1')throw Error('supervised gate lab required');
  await reset();phase='supervised';
  process.stdout.write(JSON.stringify({chromiumPid:child.pid})+'\n');
  const lines=[];let waiter=null,stdinBuffer='';
  process.stdin.setEncoding('utf8');
  process.stdin.on('data',d=>{stdinBuffer+=d;let at;while((at=stdinBuffer.indexOf('\n'))>=0){lines.push(stdinBuffer.slice(0,at));stdinBuffer=stdinBuffer.slice(at+1);}if(waiter&&lines.length){const w=waiter;waiter=null;w();}});
  const next=()=>lines.length?Promise.resolve(lines.shift()):new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('supervisor timeout')),20000);waiter=()=>{clearTimeout(t);resolve(lines.shift());};});
  let observations=0;
  for(;;){const command=await next();
   if(command==='observe'){observations++;const bounded=e=>Promise.race([ev(e),new Promise((_,reject)=>setTimeout(()=>reject(Error('observe timeout')),2000))]);process.stdout.write(JSON.stringify({value:await bounded('prose.value'),events:await bounded('imeEvents.length')})+'\n');}
   else if(command==='clear'){await ev(`prose.value='';prose.focus();true`);process.stdout.write('{"cleared":true}\n');}
   else if(command==='focus-secret'||command==='focus-prose'){await ev(`document.getElementById(${JSON.stringify(command.slice(6)==='secret'?'secret':'prose')}).focus();true`);process.stdout.write(JSON.stringify({focused:command.slice(6)})+'\n');}
   else if(command==='done'){process.stdout.write(JSON.stringify({done:true})+'\n');break;}
   else throw Error('unknown supervisor command');}
  process.stdin.pause();
  report.supervisedObservations=observations;report.supervisedFieldFocused=await ev('document.activeElement===prose');
  // Observer-side check only; the security properties are required by the Python supervisor report.
  report.observerChecks=1;report.passed=report.supervisedFieldFocused===true;
 }else if(process.env.RASKLAD_SLACK_GATE_LAB==='1'){
  // Offline Slack-session gate test (owned Chromium fixture, gate-test addon).
  // The Python parent plays the supervisor and alone holds the arm write end;
  // the driver only asks for revoke over stdout and waits for the stdin ack.
  if(!nativeWayland)throw Error('slack gate requires native Wayland');
  const control=process.env.RASKLAD_SLACK_GATE_CONTROL==='1';
  const composing=`imeEvents.filter(x=>x.type==='compositionstart').length>imeEvents.filter(x=>x.type==='compositionend').length`;
  await reset();
  phase='gate_armed';await type('word');await type('space');
  report.gateArmedCorrected=await ev(`prose.value==='привет '`);
  phase='gate_preedit';await type('word');
  report.gatePreeditShownBeforeRevoke=await ev(`prose.value==='привет ghbdtn'&&${composing}`);
  if(!control){
   phase='gate_revoke';
   const ack=new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('revoke ack timeout')),3000);process.stdin.once('data',d=>{clearTimeout(t);process.stdin.pause();resolve(String(d));});});
   process.stdout.write('{"gate":"revoke"}\n');
   if(await ack!=='revoked\n')throw Error('revoke ack');
   await new Promise(r=>setTimeout(r,400));
   report.gateRevokeClearedPreedit=await ev(`prose.value==='привет '&&!(${composing})`);
  }
  const eventsBefore=await ev('imeEvents.length');
  phase='gate_after_revoke';await type('hello');await type('word');await type('space');
  // IME delivery reaches the page as composition/beforeinput/input events, not keydown.
  report.gateInputEventsAfter=await ev(`imeEvents.length-${eventsBefore}`);
  report.gateFieldStillFocused=await ev(`document.activeElement===prose`);
  let required;
  if(control){
   // Same steps without revoke: the keys must reach the field (proves the setup can type).
   report.gateControlTextAppeared=await ev(`prose.value!=='привет '&&prose.value.startsWith('привет ')`);
   report.gateControlKeysReached=report.gateInputEventsAfter>0;
   required=['gateArmedCorrected','gatePreeditShownBeforeRevoke','gateControlTextAppeared','gateControlKeysReached','gateFieldStillFocused'];
  }else{
   report.gateRevokedNothingTyped=await ev(`prose.value==='привет '`);
   report.gateRevokedNoInputEvents=report.gateInputEventsAfter===0;
   required=['gateArmedCorrected','gatePreeditShownBeforeRevoke','gateRevokeClearedPreedit','gateRevokedNothingTyped','gateRevokedNoInputEvents','gateFieldStillFocused'];
  }
  report.gateControl=control;report.checks=required.length;report.passed=required.every(k=>report[k]===true);
 }else if(process.env.RASKLAD_DISABLE_TEST_LAB==='1'){
  if(!nativeWayland||process.env.RASKLAD_FOCUS_STRESS_LAB==='1')throw Error('exclusive native disable suite required');
  await reset();
  // No DOM mutation or scripted events after measurement starts.
  phase='disable_composition';await type('word');
  report.disableCompositionStarted=await ev(`prose.value==='ghbdtn'&&imeEvents.some(x=>x.type==='compositionstart'&&x.trusted)`);
  const offset=await ev('imeEvents.length');
  phase='disable_original';await type('lab-off');
  // Owned fixture behavioral evidence only; Chromium's compositionend may be
  // untrusted even for native delivery. Never turn this into a production ack.
  async function waitObservation(expression){const end=Date.now()+2500;while(Date.now()<end){if(await ev(expression))return true;await new Promise(r=>setTimeout(r,25));}return false;}
  report.disableOriginalSettled=await waitObservation(`(()=>{const a=imeEvents.slice(${offset});return document.activeElement===prose&&prose.value==='ghbdtn'&&a.some(x=>x.type==='compositionend'&&x.targetIndex===0)&&a.some(x=>x.trusted&&x.type==='input'&&x.targetIndex===0&&x.length===6);})()`);
  report.disableEventClasses=await ev(`imeEvents.slice(${offset}).map(x=>({type:x.type,inputType:x.inputType,length:x.length,trusted:x.trusted,targetIndex:x.targetIndex})).slice(0,16)`);
  if(!report.disableOriginalSettled)throw Error('original not settled in owned field');
  phase='disable_no_retry';await type('lab-off');
  report.disableNoDuplicateOriginal=await ev(`prose.value==='ghbdtn'`);
  phase='disable_original_undo';await type('undo');
  report.disableOriginalUndo=await ev(`prose.value===''`);
  const rawOffset=await ev('imeEvents.length');
  phase='disable_passthrough';await type('word');await type('space');
  report.disableRawPassthrough=await ev(`prose.value==='ghbdtn '&&!imeEvents.slice(${rawOffset}).some(x=>x.type==='compositionstart')`);
  phase='on_mid_token';await type('word');
  const onOffset=await ev('imeEvents.length');
  await type('lab-on');await type('word');await type('space');
  report.onMidTokenPreserved=await ev(`prose.value==='ghbdtn ghbdtnghbdtn '&&!imeEvents.slice(${onOffset}).some(x=>x.type==='compositionstart')`);
  const resumedOffset=await ev('imeEvents.length');
  phase='on_new_word';await type('word');await type('space');
  report.onNewWordCorrected=await ev(`prose.value==='ghbdtn ghbdtnghbdtn привет '&&imeEvents.slice(${resumedOffset}).some(x=>x.type==='compositionstart'&&x.trusted)`);
  phase='off_again';await type('lab-off');
  const offAgainOffset=await ev('imeEvents.length');await type('hello');
  report.offAgainPassthrough=await ev(`prose.value==='ghbdtn ghbdtnghbdtn привет hello '&&!imeEvents.slice(${offAgainOffset}).some(x=>x.type==='compositionstart')`);
  report.submissionOnly=true;report.nativeDeliveryGuarantee=false;report.syntheticOffTrigger=true;
  const required=['disableCompositionStarted','disableOriginalSettled','disableNoDuplicateOriginal','disableRawPassthrough','disableOriginalUndo','onMidTokenPreserved','onNewWordCorrected','offAgainPassthrough'];
  report.checks=required.length;report.passed=required.every(k=>report[k]===true);
 }else if(process.env.RASKLAD_PRODUCT_SUITE==='unlisted'){
  // SYS-246 product addon whose configuration does not list this app: fully inert.
  if(!nativeWayland)throw Error('native product suite required');
  const v=()=>ev('prose.value');
  phase='product_unlisted';await reset();await type('word');await type('shift2');await type('space');
  report.productUnlistedNoPreedit=await ev(`!imeEvents.some(x=>x.type==='compositionstart')`);
  report.productUnlistedTyped=await v()==='ghbdtn ';
  await type('shift2');report.productUnlistedAfterSpace=await v()==='ghbdtn ';
  const required=['productUnlistedNoPreedit','productUnlistedTyped','productUnlistedAfterSpace'];
  report.checks=required.length;report.passed=required.every(k=>report[k]===true);
 }else if(process.env.RASKLAD_PRODUCT_SUITE==='layout'){
  // SYS-226: keyboard-us and keyboard-ru in Fcitx. A correction makes the corrected
  // word's layout current, so the next word is typed in it without correction.
  if(!nativeWayland)throw Error('native product suite required');
  const v=()=>ev('prose.value');const settle=()=>new Promise(r=>setTimeout(r,250));
  phase='layout_to_ru';await reset();await type('word');await type('space');
  report.layoutFirstCorrected=await v()==='привет ';
  await settle();await type('word');await type('space');
  report.layoutSecondValue=await v();report.layoutSecondTypedInRu=report.layoutSecondValue==='привет привет ';
  // A single letter is never corrected: only the switched layout makes x into ч.
  await type('xspace');report.layoutLetterValue=await v();report.layoutLetterIsRu=report.layoutLetterValue==='привет привет ч ';
  phase='layout_to_us';await settle();await type('hello');
  report.layoutThirdValue=await v();report.layoutThirdCorrected=report.layoutThirdValue==='привет привет ч hello ';
  await settle();await type('hello');
  report.layoutFourthValue=await v();report.layoutFourthTypedInUs=report.layoutFourthValue==='привет привет ч hello hello ';
  await type('xspace');report.layoutLetterUsValue=await v();report.layoutLetterIsUs=report.layoutLetterUsValue==='привет привет ч hello hello x ';
  // Owner decision 2026-11-23: double Alt is detected by Hyprland, which runs
  // rasklad-layout-toggle; here the same packaged script runs directly. It switches
  // us <-> ru, and a word in progress is committed as typed, then the layout switches.
  phase='toggle';const base='привет привет ч hello hello x ';
  const toggle=()=>execFileSync('/lab/rasklad-layout-toggle',[],{stdio:['ignore','ignore','pipe'],timeout:5000});
  toggle();await settle();await type('xspace');report.toggleToRuValue=await v();report.toggleToRu=report.toggleToRuValue===base+'ч ';
  toggle();await settle();await type('xspace');report.toggleToUsValue=await v();report.toggleToUs=report.toggleToUsValue===base+'ч x ';
  await type('word');toggle();await settle();await type('xspace');report.toggleWordValue=await v();report.toggleWordKeptThenRu=report.toggleWordValue===base+'ч x ghbdtnч ';
  const required=['layoutFirstCorrected','layoutSecondTypedInRu','layoutLetterIsRu','layoutThirdCorrected','layoutFourthTypedInUs','layoutLetterIsUs','toggleToRu','toggleToUs','toggleWordKeptThenRu'];
  report.checks=required.length;report.passed=required.every(k=>report[k]===true);
 }else if(process.env.RASKLAD_PRODUCT_SUITE==='exceptions'){
  // SYS-227: exceptions "Привет" and "rfr" are never corrected automatically, in any
  // case or with punctuation after them; other words still are; the gesture still works.
  if(!nativeWayland)throw Error('native product suite required');
  const v=()=>ev('prose.value');
  phase='exception_word';await reset();await type('word');await type('space');report.exceptWordValue=await v();report.exceptWord=report.exceptWordValue==='ghbdtn ';
  await reset();await type('capword');report.exceptCapitalValue=await v();report.exceptCapital=report.exceptCapitalValue==='Ghbdtn ';
  await reset();await type('wordcomma');report.exceptCommaValue=await v();report.exceptComma=report.exceptCommaValue==='ghbdtn, ';
  await reset();await type('shortword');report.exceptShortValue=await v();report.exceptShort=report.exceptShortValue==='rfr ';
  // "пою" (gj.) then a comma: the punctuation fallback must not correct "gj" to "по".
  await reset();await type('poyucomma');report.exceptLetterKeyValue=await v();report.exceptLetterKey=report.exceptLetterKeyValue==='gj., ';
  phase='exception_others';await reset();await type('zhurnal');await type('space');report.exceptOtherValue=await v();report.exceptOtherCorrected=report.exceptOtherValue==='журнал ';
  phase='exception_gesture';await reset();await type('word');await type('shift2');await type('space');report.exceptGestureValue=await v();report.exceptGesture=report.exceptGestureValue==='привет ';
  const required=['exceptWord','exceptCapital','exceptComma','exceptShort','exceptLetterKey','exceptOtherCorrected','exceptGesture'];
  report.checks=required.length;report.passed=required.every(k=>report[k]===true);
 }else if(process.env.RASKLAD_PRODUCT_SUITE==='gestures'){
  // SYS-227: automatic correction switched off: words stay as typed on every word end,
  // in both layouts; double Shift still converts the word being typed.
  if(!nativeWayland)throw Error('native product suite required');
  const v=()=>ev('prose.value');
  phase='gestures_space';await reset();await type('word');await type('space');report.offSpaceValue=await v();report.offSpace=report.offSpaceValue==='ghbdtn ';
  await reset();await type('shortword');report.offShortValue=await v();report.offShort=report.offShortValue==='rfr ';
  await reset();await type('wordcomma');report.offCommaValue=await v();report.offComma=report.offCommaValue==='ghbdtn, ';
  await reset();await type('word');await type('enter');report.offEnterValue=await v();report.offEnter=report.offEnterValue==='ghbdtn\n';
  phase='gestures_shift';await reset();await type('word');await type('shift2');report.offGesturePreedit=await v()==='привет';
  await type('space');report.offGestureValue=await v();report.offGestureCommit=report.offGestureValue==='привет ';
  phase='gestures_ru';await reset();await type('rugroup');await type('hello');report.offRuValue=await v();report.offRu=report.offRuValue==='руддщ ';
  await type('usgroup');
  const required=['offSpace','offShort','offComma','offEnter','offGesturePreedit','offGestureCommit','offRu'];
  report.checks=required.length;report.passed=required.every(k=>report[k]===true);
 }else if(process.env.RASKLAD_PRODUCT_SUITE==='listed'){
  // SYS-226 product addon: automatic correction on Space by the reviewed profile, both
  // directions; double Shift right after the Space restores; the gesture still works.
  if(!nativeWayland)throw Error('native product suite required');
  const v=()=>ev('prose.value');const settle=()=>new Promise(r=>setTimeout(r,150));
  phase='auto_us_ru';await reset();await type('word');await type('space');
  report.autoUsToRu=await v()==='привет ';
  // Sway's Chromium never confirms the committed word (no fresh surrounding text), so
  // there the replacement must only be refused; Hyprland must restore and redo.
  const strictUndo=process.env.RASKLAD_HYPRLAND_IME_LAB==='1';report.strictUndo=strictUndo;
  await settle();await type('shift2');report.autoUndoValue=await v();report.autoUndoRestored=report.autoUndoValue==='ghbdtn ';
  await settle();await type('shift2');report.autoRedoValue=await v();
  report.autoUndo=strictUndo?report.autoUndoRestored&&report.autoRedoValue==='привет ':
    ['привет ','ghbdtn '].includes(report.autoUndoValue)&&['привет ','ghbdtn '].includes(report.autoRedoValue);
  phase='auto_english_kept';await reset();await type('hello');report.autoEnglishKept=await v()==='hello ';
  await reset();await type('hellocomma');report.autoEnglishCommaKept=await v()==='hello, ';
  phase='auto_punct_key';await reset();await type('zhurnal');await type('space');
  report.autoZhurnalValue=await v();report.autoZhurnal=report.autoZhurnalValue==='журнал ';
  phase='auto_comma';await reset();await type('wordcomma');report.autoBeforeComma=await v()==='привет, ';
  phase='auto_exclaim';await reset();await type('word');await type('exclaim');await type('space');
  report.autoBeforeExclaimValue=await v();report.autoBeforeExclaim=report.autoBeforeExclaimValue==='привет! ';
  phase='auto_enter';await reset();await type('word');await type('enter');report.autoBeforeEnter=await v()==='привет\n';
  phase='auto_english_exclaim';await reset();await type('hello');await type('backspace');await type('exclaim');report.autoEnglishExclaimKept=await v()==='hello!';
  // BackSpace on an empty word above latched bypass until a delimiter.
  phase='auto_capital';await type('space');await reset();await type('capword');report.autoCapitalValue=await v();report.autoCapital=report.autoCapitalValue==='Привет ';
  phase='auto_short';await reset();await type('shortword');report.autoShortValue=await v();report.autoShort=report.autoShortValue==='как ';
  phase='auto_hyphen';await reset();await type('hyphen');report.autoHyphenValue=await v();report.autoHyphen=report.autoHyphenValue==='куда-то ';
  phase='gesture_choice_kept';await reset();await type('word');await type('shift2');await type('shift2');await type('space');
  report.gestureChoiceKept=await v()==='ghbdtn ';
  await type('word');await type('space');report.autoAfterGestureWord=await v()==='ghbdtn привет ';
  phase='gesture_convert';await reset();await type('word');await type('shift2');
  report.gesturePreedit=await v()==='привет';await type('space');report.gestureCommit=await v()==='привет ';
  phase='flush_no_auto';await reset();await type('word');await type('escape');
  report.flushNoAuto=await v()==='ghbdtn';await type('space');
  phase='ru_en';await reset();await type('rugroup');await type('hello');
  report.autoRuToUsValue=await v();report.autoRuToUs=report.autoRuToUsValue==='hello ';
  await settle();await type('shift2');report.autoRuUndoValue=await v();report.autoRuUndoRestored=report.autoRuUndoValue==='руддщ ';
  report.autoRuUndo=strictUndo?report.autoRuUndoRestored:['hello ','руддщ '].includes(report.autoRuUndoValue);
  phase='ru_kept';await reset();await type('word');await type('space');
  report.autoRussianKept=await v()==='привет ';
  phase='ru_gesture';await reset();await type('word');await type('shift2');
  report.ruGesturePreedit=await v()==='ghbdtn';await type('space');report.ruGestureCommit=await v()==='ghbdtn ';
  await type('usgroup');
  phase='auto_password';await reset('secret');await type('word');await type('space');
  report.autoPasswordUntouched=await ev(`secret.value==='ghbdtn '&&!imeEvents.some(x=>x.type==='compositionstart')`);
  phase='auto_focus';await reset();await type('word');await ev(`other.focus();imeEvents=[]`);await settle();await type('space');
  report.autoFocusNoLeak=await ev(`other.value===' '`);
  phase='auto_shift_letter';await reset();await type('word');await type('shifta');await type('space');
  report.autoShiftLetterOriginal=await v()==='ghbdtnA ';
  const required=['autoUsToRu','autoUndo','autoEnglishKept','autoEnglishCommaKept','autoZhurnal','autoBeforeComma','autoBeforeExclaim','autoBeforeEnter','autoEnglishExclaimKept','autoCapital','autoShort','autoHyphen','gestureChoiceKept','autoAfterGestureWord','gesturePreedit','gestureCommit','flushNoAuto','autoRuToUs','autoRuUndo','autoRussianKept','ruGesturePreedit','ruGestureCommit','autoPasswordUntouched','autoFocusNoLeak','autoShiftLetterOriginal'];
  report.checks=required.length;report.passed=required.every(k=>report[k]===true);
 }else if(process.env.RASKLAD_MANUAL_LAB==='1'){
  // SYS-225 slice 1: no automatic correction; double Shift toggles the preedit word.
  if(!nativeWayland)throw Error('native manual suite required');
  const v=()=>ev('prose.value');
  phase='manual_convert';await reset();await type('word');await type('shift2');
  report.manualPreeditConverted=await v()==='привет';
  await type('space');report.manualConvertedCommit=await v()==='привет ';
  phase='manual_undo';await reset();await type('word');await type('shift2');await type('shift2');
  report.manualUndoPreedit=await v()==='ghbdtn';
  await type('space');report.manualUndoCommit=await v()==='ghbdtn ';
  phase='manual_no_gesture';await reset();await type('word');await type('space');
  report.manualNoAutoCorrection=await v()==='ghbdtn ';
  phase='manual_shift_letter';await reset();await type('word');await type('shifta');await type('space');
  report.manualShiftLetterOriginal=await v()==='ghbdtnA ';
  phase='manual_slow';await reset();await type('word');await type('shiftslow');await type('space');
  report.manualSlowIgnored=await v()==='ghbdtn ';
  phase='manual_empty';await reset();await type('shift2');
  report.manualEmptyNothing=await v()==='';
  // A Shift tap before a word does not stop the word from being composed.
  await type('word');await type('shift2');await type('space');report.manualShiftBeforeWord=await v()==='привет ';
  phase='manual_backspace';await reset();await type('word');await type('shift2');await type('backspace');
  report.manualBackspaceValue=await v();report.manualBackspaceConverted=report.manualBackspaceValue==='приве';
  await type('space');report.manualBackspaceCommit=await v()==='приве ';
  // б ю ж э х ъ ё keys are part of the word; without the gesture English punctuation stays.
  phase='manual_punct';await reset();await type('zhurnal');await type('shift2');await type('space');
  report.manualZhurnal=await v()==='журнал ';
  await reset();await type('khorosho');await type('shift2');await type('space');report.manualKhorosho=await v()==='хорошо ';
  await reset();await type('yolka');await type('shift2');await type('space');report.manualYolka=await v()==='ёлка ';
  await reset();await type('hellocomma');report.manualEnglishCommaKept=await v()==='hello, ';
  // Non-Space flush path (Escape here; Enter/Tab/punctuation share it): commits what is shown.
  phase='manual_flush';await reset();await type('word');await type('shift2');await type('escape');
  report.manualFlushCommitsShown=await v()==='привет';
  await type('space'); // Escape latches bypass; a boundary ends it before the next case
  // After Space the addon may replace only its own last word, and only when the client
  // reports surrounding text proving that word + space sits right before the cursor.
  // Measured: this Chromium under closed Sway reports none, so here the gesture must
  // refuse and leave the field untouched (the success path needs a client with
  // surrounding text, e.g. Slack under Hyprland).
  phase='manual_after_space';await reset();await type('word');await type('space');await type('shift2');
  report.manualAfterSpaceValue=await v();
  // Safe outcomes only: untouched (no surrounding text) or exactly the other form.
  report.manualAfterSpaceReplaced=report.manualAfterSpaceValue==='привет ';
  await type('shift2');report.manualAfterSpaceSecondValue=await v();
  report.manualAfterSpaceRestored=report.manualAfterSpaceReplaced&&report.manualAfterSpaceSecondValue==='ghbdtn ';
  report.manualAfterSpaceUnknownRefused=['ghbdtn ','привет '].includes(report.manualAfterSpaceValue)&&
    ['ghbdtn ','привет '].includes(report.manualAfterSpaceSecondValue)&&await ev(`imeEvents.some(x=>x.type==='compositionstart')`);
  phase='manual_after_space_dom_cleared';await reset();await type('word');await type('space');
  await ev(`prose.value=''`);await type('shift2');report.manualClearedFieldUntouched=await v()==='';
  phase='manual_after_space_key_between';await reset();await type('word');await type('space');await type('backspace');await type('shift2');
  report.manualKeyBetweenNoReplace=await v()==='ghbdtn';
  await type('space'); // BackSpace latches bypass; a boundary ends it before the next case
  // Right Shift works the same; Ctrl+Shift and both Shifts together are combinations, never the gesture.
  phase='manual_shift_right';await reset();await type('word');await type('shiftr2');await type('space');
  report.manualShiftRightValue=await v();report.manualShiftRight=report.manualShiftRightValue==='привет ';
  phase='manual_ctrl_shift';await reset();await type('word');await type('ctrlshift2');await type('space');
  report.manualCtrlShiftIgnored=await v()==='ghbdtn ';
  await type('space'); // a passed-through modifier may latch bypass; a boundary ends it
  phase='manual_both_shifts';await reset();await type('word');await type('shiftlr2');await type('space');
  report.manualBothShiftsIgnored=await v()==='ghbdtn ';
  await type('space');
  // Caret moved after Space (as a mouse click would): the word is no longer replaceable,
  // even after the caret comes back to the end.
  phase='manual_after_space_caret';await reset();await type('word');await type('space');await new Promise(r=>setTimeout(r,150));
  await ev(`prose.setSelectionRange(0,0)`);await new Promise(r=>setTimeout(r,150));
  await ev(`prose.setSelectionRange(prose.value.length,prose.value.length)`);await new Promise(r=>setTimeout(r,150));
  await type('shift2');report.manualCaretMovedNoReplace=await v()==='ghbdtn ';
  // Typing straight on after a replacement starts a fresh word; nothing else changes.
  phase='manual_after_replace_typing';await reset();await type('word');await type('space');await type('shift2');
  const first=await v();await type('word');await type('space');
  report.manualTypingAfterReplace=['привет ','ghbdtn '].includes(first)&&await v()===first+'ghbdtn ';
  phase='manual_password';await reset('secret');await type('word');await type('shift2');await type('space');
  report.manualPasswordUntouched=await ev(`secret.value==='ghbdtn '&&!imeEvents.some(x=>x.type==='compositionstart')`);
  phase='manual_focus';await reset();await type('word');await type('shift2');await ev(`other.focus();imeEvents=[]`);await new Promise(r=>setTimeout(r,120));await type('space');
  report.manualFocusNoLeak=await ev(`other.value===' '`);
  const required=['manualPreeditConverted','manualConvertedCommit','manualUndoPreedit','manualUndoCommit','manualNoAutoCorrection','manualShiftLetterOriginal','manualSlowIgnored','manualEmptyNothing','manualShiftBeforeWord','manualBackspaceConverted','manualBackspaceCommit','manualFlushCommitsShown','manualZhurnal','manualKhorosho','manualYolka','manualEnglishCommaKept','manualAfterSpaceUnknownRefused','manualClearedFieldUntouched','manualKeyBetweenNoReplace','manualPasswordUntouched','manualFocusNoLeak','manualShiftRight','manualCtrlShiftIgnored','manualBothShiftsIgnored','manualCaretMovedNoReplace','manualTypingAfterReplace'];
  report.checks=required.length;report.passed=required.every(k=>report[k]===true);
 }else{
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
  if(process.env.RASKLAD_ADDON_WITNESS_LAB==='1')sourceNames.push('addon-witness.mjs');
  if(process.env.RASKLAD_SERVER_ROUTE_WITNESS_LAB==='1')sourceNames.push('server-route-witness.mjs');
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
  let addonWitness=null,addonWitnessFactory=null;
  if(process.env.RASKLAD_ADDON_WITNESS_LAB==='1'){
   const {createAddonWitness}=await import('file:///lab/addon-witness.mjs');
   addonWitnessFactory=()=>createAddonWitness({query:async()=>execFileSync('/usr/bin/gdbus',[
    'call','--session','--dest','org.fcitx.Fcitx5','--object-path','/org/rasklad/LabWitness',
    '--method','org.rasklad.LabWitness1.GetStats'],{encoding:'utf8',timeout:1000,maxBuffer:1024,stdio:['ignore','pipe','ignore']}),noQuery:process.env.RASKLAD_ADDON_WITNESS_NO_QUERY_LAB==='1',coreEntry:process.env.RASKLAD_CORE_ENTRY_WITNESS_LAB==='1',grabEntry:process.env.RASKLAD_GRAB_ENTRY_WITNESS_LAB==='1'});
  }
  let serverWitnessFactory=null;
  if(process.env.RASKLAD_SERVER_ROUTE_WITNESS_LAB==='1'){
   const {createServerRouteWitness}=await import('file:///lab/server-route-witness.mjs');
   serverWitnessFactory=()=>createServerRouteWitness({grabEpoch:process.env.RASKLAD_GRAB_EPOCH_WITNESS_LAB==='1'});
  }
  const repeatRaw=process.env.RASKLAD_FOCUS_REPEATS_LAB??'1';
  const repeatCount=/^[1-5]$/.test(repeatRaw)?Number(repeatRaw):NaN;
  report.focusStress=await runFocusScenarios({ev:focusEv,type,reset,spaceTelemetry,addonWitnessFactory,serverWitnessFactory,repeatCount},endEventPolicy);
  report.focusStress.instrumentedAddonTrial=process.env.RASKLAD_ADDON_WITNESS_LAB==='1';
  report.focusStress.instrumentedServerRouteTrial=serverWitnessFactory!==null;
  report.focusStress.instrumentedGrabEpochTrial=process.env.RASKLAD_GRAB_EPOCH_WITNESS_LAB==='1';
  report.focusStress.instrumentedGrabEntryTrial=process.env.RASKLAD_GRAB_ENTRY_WITNESS_LAB==='1';
  report.focusStress.instrumentedCoreEntryTrial=process.env.RASKLAD_CORE_ENTRY_WITNESS_LAB==='1';
  report.focusStress.addonWitnessQueriesEnabled=addonWitnessFactory!==null&&process.env.RASKLAD_ADDON_WITNESS_NO_QUERY_LAB!=='1';
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
 }
}catch(e){report.passed=false;report.failedPhase=phase;report.browserExit=lastExit;report.failure=String(e.message).startsWith('Command failed:')?'private_key_driver_failed':String(e.message).slice(0,80);}
finally{
 try{await Promise.race([call('Browser.close'),new Promise(r=>setTimeout(r,1000))]);}catch{}
 if(alive)child.kill('SIGTERM');
 await Promise.race([exited,new Promise(r=>setTimeout(r,1000))]);if(alive)child.kill('SIGKILL');if(vk){vk.kill('SIGTERM');await Promise.race([vkExited,new Promise(r=>setTimeout(r,500))]);if(vkAlive){vk.kill('SIGKILL');await vkExited;}}clearTimeout(deadline);
}
if(report.passed===false&&nativeWayland&&['baseline','keep-grab'].includes(process.env.RASKLAD_PRIVATE_FRONTEND_VARIANT))report._privateBrowserDiagnostic=diagnostics.slice(-3500);
console.log(JSON.stringify(report));process.exitCode=report.passed?0:1;
