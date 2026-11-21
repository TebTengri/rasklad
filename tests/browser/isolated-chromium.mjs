// SPDX-License-Identifier: MIT
// Shared closed Chromium/CDP fixture. Never uses the host browser profile.
import {spawn} from 'node:child_process';
import {resolve} from 'node:path';
export function startIsolatedChromium(root,timeoutMs=45000,fixtureDirectory=resolve(root,'build/browser-fixture')){
const args = ['--unshare-all','--die-with-parent','--new-session',
  '--ro-bind','/usr','/usr','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
  '--proc','/proc','--dev','/dev','--tmpfs','/tmp','--dir','/home','--dir','/home/test',
  '--dir','/etc','--ro-bind','/etc/fonts','/etc/fonts','--dir','/lab','--dir','/lab/build',
  '--ro-bind',fixtureDirectory,'/lab/build/browser-fixture',
  '--setenv','HOME','/home/test','--setenv','XDG_RUNTIME_DIR','/tmp',
  '--unsetenv','DBUS_SESSION_BUS_ADDRESS','--','/usr/lib/chromium/chromium',
  '--headless','--disable-gpu','--disable-background-networking','--disable-component-update',
  '--no-first-run','--no-default-browser-check','--user-data-dir=/tmp/profile',
  '--remote-debugging-pipe','about:blank'];
const child = spawn('/usr/bin/bwrap',args,{stdio:['ignore','ignore','pipe','pipe','pipe'],env:{PATH:'/usr/bin'}});
let sequence=0, buffer='', stderr='', alive=true; const pending=new Map(),events=new Map();
child.stderr.on('data',d=>{stderr=(stderr+d).slice(-4000);});
const exited = new Promise(resolve=>child.once('exit',(code,signal)=>{
  alive=false;
  for(const {reject} of pending.values()) reject(new Error('isolated browser exited'));
  for(const callbacks of events.values())for(const entry of callbacks)entry.reject(new Error("isolated browser exited"));
  events.clear();pending.clear(); resolve({code,signal});
}));
for(const pipe of [child.stdio[3],child.stdio[4]]) pipe.on('error',()=>{});
child.stdio[4].on('data',data=>{
  buffer+=data; let at;
  while((at=buffer.indexOf('\0'))>=0){
    const message=JSON.parse(buffer.slice(0,at)); buffer=buffer.slice(at+1);
    if(!message.id && message.method){const key=(message.sessionId||'')+':'+message.method;const entries=events.get(key)||[];events.delete(key);for(const entry of entries)entry.resolve(message.params);}
    if(message.id){const entry=pending.get(message.id);pending.delete(message.id);
      if(message.error) entry?.reject(new Error('CDP '+message.error.message));else entry?.resolve(message.result);}
  }
});
function call(method,params={},sessionId){return new Promise((resolve,reject)=>{
  if(!alive){reject(new Error('isolated browser already closed'));return;}
  const id=++sequence;pending.set(id,{resolve,reject});
  child.stdio[3].write(JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})})+'\0');
});}
const deadline=setTimeout(()=>child.kill('SIGKILL'),timeoutMs);

return Object.freeze({call,waitForEvent(method,sessionId=''){return new Promise((resolve,reject)=>{const key=sessionId+':'+method;const entries=events.get(key)||[];entries.push({resolve,reject});events.set(key,entries);});},async close(){
  try{await call('Browser.close');}catch{}
  const result=await exited;clearTimeout(deadline);
  if(result.code!==0)throw new Error('isolated browser failed: '+stderr);
}});
}
