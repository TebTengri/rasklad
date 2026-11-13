// SPDX-License-Identifier: MIT
// Closed synthetic browser integration. No host profile, bus, input or network.
import {spawn, execFileSync} from 'node:child_process';
import {writeFileSync,readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve, dirname} from 'node:path';
import assert from 'node:assert/strict';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
execFileSync(resolve(root, 'bin/build-browser-fixture'), {stdio:'ignore'});
const args = ['--unshare-all','--die-with-parent','--new-session',
  '--ro-bind','/usr','/usr','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
  '--proc','/proc','--dev','/dev','--tmpfs','/tmp','--dir','/home','--dir','/home/test',
  '--dir','/etc','--ro-bind','/etc/fonts','/etc/fonts','--ro-bind',root,'/lab',
  '--setenv','HOME','/home/test','--setenv','XDG_RUNTIME_DIR','/tmp',
  '--unsetenv','DBUS_SESSION_BUS_ADDRESS','--','/usr/lib/chromium/chromium',
  '--headless','--disable-gpu','--disable-background-networking','--disable-component-update',
  '--no-first-run','--no-default-browser-check','--user-data-dir=/tmp/profile',
  '--remote-debugging-pipe','about:blank'];
const child = spawn('/usr/bin/bwrap',args,{stdio:['ignore','ignore','pipe','pipe','pipe'],env:{PATH:'/usr/bin'}});
let sequence=0, buffer='', stderr='', alive=true; const pending=new Map();
child.stderr.on('data',d=>{stderr=(stderr+d).slice(-4000);});
const exited = new Promise(resolve=>child.once('exit',(code,signal)=>{
  alive=false;
  for(const {reject} of pending.values()) reject(new Error('isolated browser exited'));
  pending.clear(); resolve({code,signal});
}));
for(const pipe of [child.stdio[3],child.stdio[4]]) pipe.on('error',()=>{});
child.stdio[4].on('data',data=>{
  buffer+=data; let at;
  while((at=buffer.indexOf('\0'))>=0){
    const message=JSON.parse(buffer.slice(0,at)); buffer=buffer.slice(at+1);
    if(message.id){const entry=pending.get(message.id);pending.delete(message.id);
      if(message.error) entry?.reject(new Error('CDP '+message.error.message));else entry?.resolve(message.result);}
  }
});
function call(method,params={},sessionId){return new Promise((resolve,reject)=>{
  if(!alive){reject(new Error('isolated browser already closed'));return;}
  const id=++sequence;pending.set(id,{resolve,reject});
  child.stdio[3].write(JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})})+'\0');
});}
const deadline=setTimeout(()=>child.kill('SIGKILL'),45000);
let passed=0;
try {
  const version=await call('Browser.getVersion');
  const {targetId}=await call('Target.createTarget',{url:'file:///lab/build/browser-fixture/fixture.html'});
  const {sessionId}=await call('Target.attachToTarget',{targetId,flatten:true});
  const c=(method,params)=>call(method,params,sessionId);
  async function evaluate(expression){const x=await c('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
    if(x.exceptionDetails)throw new Error('fixture evaluation failed');return x.result.value;}
  await c('Emulation.setDeviceMetricsOverride',{width:1000,height:1050,deviceScaleFactor:1,mobile:false});
  await c('Runtime.enable');
  // Navigation completion without an arbitrary sleep.
  await evaluate(`new Promise((resolve,reject)=>{const end=Date.now()+5000;function ready(){if(globalThis.fixtureAdapter)return resolve(true);if(Date.now()>end)return reject(Error('fixture timeout'));setTimeout(ready,20);}ready();})`);
  async function reset(id='prose',setup=''){
    await evaluate(`fixtureAdapter.dispose();document.body.querySelectorAll('textarea').forEach(x=>{x.value='';x.readOnly=false;});
      globalThis.calls=0;globalThis.localDecision=t=>{calls++;return fixtureRouter.decideUs(t);};
      globalThis.fixtureAdapter=RaskladBrowser.createAdapter({document,decide:t=>localDecision(t)});
      for(const id of ['prose','code','protected'])fixtureAdapter.register(document.getElementById(id),{role:'prose',layout:'us'});
      globalThis.field=document.getElementById(${JSON.stringify(id)});field.focus();${setup}`);
  }
  async function type(text){for(const ch of text){await c('Input.dispatchKeyEvent',{type:'keyDown',key:ch,text:ch,unmodifiedText:ch});
    await c('Input.dispatchKeyEvent',{type:'keyUp',key:ch});}}
  async function check(name,expression){assert.equal(await evaluate(expression),true,name);passed++;}
  await reset();await type('ghbdtn');await check('letters committed before Space',`field.value==='ghbdtn'`);
  await type(' ');await check('Space correction',`field.value==='привет ' && calls===1`);
  const preview=await c('Page.captureScreenshot',{format:'png'});
  writeFileSync(resolve(root,'build/browser-fixture/preview.png'),Buffer.from(preview.data,'base64'));
  // Native browser editing command via actual keyboard shortcut, not fixture undo.
  await c('Input.dispatchKeyEvent',{type:'rawKeyDown',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:2});
  await c('Input.dispatchKeyEvent',{type:'keyUp',key:'z',code:'KeyZ',windowsVirtualKeyCode:90,modifiers:2});
  await check('native Ctrl+Z restores original word',`field.value==='ghbdtn'`);
  await type(' ');await check('Undo followed by Space keeps original word',`field.value==='ghbdtn '`);
  await reset();await type('ghbdtn x');await check('next character preserved',`field.value==='привет x' && field.selectionStart===8`);
  for(const id of ['unknown','code','protected']){await reset(id);await type('ghbdtn ');await check(id+' never queried',`field.value==='ghbdtn ' && calls===0`);}
  await reset();await type('https://ghbdtn ');await check('URL unchanged',`field.value==='https://ghbdtn ' && calls===0`);
  await reset();await type('/ghbdtn ');await check('path unchanged',`field.value==='/ghbdtn ' && calls===0`);
  await reset();await type('hello ');await check('abstention preserves text',`field.value==='hello '`);
  await reset();await type('test abcd ');await check('shared ambiguity and dictionary-only never auto-correct',`field.value==='test abcd '`);
  await check('Suggest is explicit only',`fixtureRouter.route({field:'prose',layout:'us',token:'abcd',profile:'requestedHybrid'}).action==='suggest' && fixtureRouter.decideUs('abcd')===null`);
  await reset('prose',`localDecision=t=>{calls++;field.readOnly=true;return 'привет';};`);
  await type('ghbdtn ');await check('context changed during evidence',`field.value==='ghbdtn' && calls===1`);
  await reset('prose',`localDecision=t=>{calls++;field.value='newer';field.setSelectionRange(5,5);return 'привет';};`);
  await type('ghbdtn ');await check('stale snapshot refused',`field.value==='newer ' && calls===1`);
  await reset('prose',`localDecision=t=>{calls++;return Promise.resolve('привет');};`);
  await type('ghbdtn ');await check('async evidence rejected',`field.value==='ghbdtn ' && fixtureAdapter.status(field)==='disabled'`);
  await reset();await type('ghbdtn');
  await evaluate(`field.dispatchEvent(new CompositionEvent('compositionstart',{bubbles:true}));`);
  await type(' ');await check('composition bypass',`field.value==='ghbdtn ' && calls===0`);
  await reset();await type('ghbdtn');
  await evaluate(`field.dispatchEvent(new InputEvent('beforeinput',{data:' ',inputType:'insertText',bubbles:true,cancelable:true}));`);
  await check('untrusted event ignored',`field.value==='ghbdtn' && calls===0`);
  await reset();await type('ghbdtn');
  await evaluate(`field.setSelectionRange(0,6);`);await type(' ');
  await check('selection preserved native operation',`field.value===' ' && calls===0`);
  await reset();await type('ghbdtn');await evaluate(`fixtureAdapter.dispose();`);await type(' ');
  await check('disposal leaves typing intact',`field.value==='ghbdtn '`);
  await reset('prose', `globalThis.originalExec=document.execCommand;document.execCommand=()=>false;`);
  await type('ghbdtn ');await check('failed no-op keeps native Space',`field.value==='ghbdtn ' && field.selectionStart===7 && fixtureAdapter.status(field)==='disabled'`);
  await evaluate(`document.execCommand=originalExec;`);
  await reset('prose', `document.execCommand=()=>{throw Error('fixture failure');};`);
  await type('ghbdtn ');await check('thrown no-op keeps native Space',`field.value==='ghbdtn ' && fixtureAdapter.status(field)==='disabled'`);
  await evaluate(`document.execCommand=originalExec;`);
  await reset('prose', `globalThis.ownerUpdate=()=>{if(field.value==='привет ')field.value='owner update';};field.addEventListener('input',ownerUpdate);`);
  await type('ghbdtn x');await check('application update never rolled back',`field.value==='owner updatex' && fixtureAdapter.status(field)==='disabled'`);
  await evaluate(`field.removeEventListener('input',ownerUpdate);`);
  await reset('prose', `field.value='a'.repeat(65537);field.setSelectionRange(65537,65537);`);
  await type(' ');await check('document limit before evidence',`field.value.length===65538 && calls===0`);
  await reset('prose', `const form=document.createElement('form');form.id='external-form';document.body.append(form);field.setAttribute('form',form.id);const password=document.createElement('input');password.type='password';password.setAttribute('form',form.id);password.id='external-password';document.body.append(password);`);
  await type('ghbdtn ');await check('external form-associated password blocks evidence',`field.value==='ghbdtn ' && calls===0`);
  await evaluate(`field.removeAttribute('form');document.getElementById('external-password').remove();document.getElementById('external-form').remove();`);
  await reset('prose', `globalThis.nativeSelect=field.setSelectionRange;field.setSelectionRange=function(a,b){nativeSelect.call(this,a,b);if(a!==b)this.readOnly=true;};`);
  await type('ghbdtn ');await check('post-selection rejection collapses own range',`field.value==='ghbdtn' && field.selectionStart===field.selectionEnd && fixtureAdapter.status(field)==='disabled'`);
  await evaluate(`field.setSelectionRange=nativeSelect;field.readOnly=false;`);await type('x');
  await check('next character cannot erase refused token',`field.value==='ghbdtnx'`);
  await reset('prose', `globalThis.nativeValue=Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype,'value');globalThis.protectedReads=0;Object.defineProperty(field,'value',{configurable:true,get(){if(this.getAttribute('autocomplete')==='one-time-code')protectedReads++;return nativeValue.get.call(this);},set(v){nativeValue.set.call(this,v);}});globalThis.protectOnInput=()=>{if(field.value==='привет ')field.setAttribute('autocomplete','one-time-code');};field.addEventListener('input',protectOnInput);`);
  await type('ghbdtn ');await check('protected context after native edit disables',`nativeValue.get.call(field)==='привет ' && fixtureAdapter.status(field)==='disabled' && protectedReads===0`);
  await evaluate(`field.removeEventListener('input',protectOnInput);field.removeAttribute('autocomplete');delete field.value;`);
  const timing={};
  for(const mode of ['baseline','adapter']){
    await reset();if(mode==='baseline')await evaluate(`fixtureAdapter.dispose();`);
    await evaluate(`globalThis.samples=[];globalThis.measure=e=>{if(e.isTrusted&&e.inputType==='insertText'&&e.data===' '){const start=performance.now();queueMicrotask(()=>samples.push(performance.now()-start));}};document.addEventListener('beforeinput',measure,true);`);
    await type('ghbdtn '.repeat(50));
    timing[mode]=await evaluate(`(()=>{document.removeEventListener('beforeinput',measure,true);const a=samples.sort((a,b)=>a-b);return {samples:a.length,medianMs:a[Math.floor(a.length/2)],p95Ms:a[Math.floor(a.length*.95)],maxMs:a.at(-1)};})()`);
    await check(mode+' timing text integrity',mode==='adapter'?`field.value==='привет '.repeat(50)`:`field.value==='ghbdtn '.repeat(50)`);
  }
  const heap=await c('Runtime.getHeapUsage');
  const nativeGold=JSON.parse(readFileSync(resolve(root,'build/browser-fixture/synthetic-conformance.json'),'utf8'));
  const browserConformance=await evaluate('('+function(gold){
    let scores=0,decisions=0;
    for(const entry of gold.models){
      const bytes=Uint8Array.from(atob(entry.bytesBase64),c=>c.charCodeAt(0));
      const model=RaskladScorer.loadModel(bytes,RaskladPolicyData.modelSpec);
      for(const item of entry.cases){
        const score=model.score(item.keys),wanted=item.score;
        if(score.ok!==wanted.ok || score.keyCount!==wanted.keyCount || score.steps!==wanted.steps ||
          score.raw[0]!==wanted.raw[0] || score.raw[1]!==wanted.raw[1])throw Error('native score mismatch');
        scores++;
        for(const layout of ['us','ru']){if(model.decide(score,layout)!==item[layout])throw Error('native decision mismatch');decisions++;}
      }
      for(const item of gold.boundaries)for(const layout of ['us','ru']){
        if(model.decide(item.score,layout)!==item[layout])throw Error('native boundary mismatch');decisions++;
      }
      model.dispose();
    }
    return {scores,decisions};
  }.toString()+')('+JSON.stringify(nativeGold)+')');
  assert.deepEqual(browserConformance,{scores:1176,decisions:2464});passed++;
  console.log(JSON.stringify({passed,browser:version.product,browserConformance,timing,jsHeapUsedBytesBeforeConformance:heap.usedSize,scope:'synthetic owned textarea, numeric model and dictionary fixtures; no real data or host session'}));
} catch(error){console.error(JSON.stringify({passed,error:error.message}));process.exitCode=1;}
finally {
  try{await call('Browser.close');}catch{}
  const result=await exited;clearTimeout(deadline);
  if(result.code!==0){console.error(JSON.stringify({browserExit:result,diagnostic:stderr}));process.exitCode=1;}
}
