// SPDX-License-Identifier: MIT
// Private lab only: aggregate ALL-private-VK/server emission attempts, not delivery proof.
import {openSync,closeSync,fstatSync,readSync,constants} from 'node:fs';
export const SERVER_ROUTE_COUNTERS=Object.freeze(['vkSpacePress','vkSpaceRelease','grabSpacePress',
 'grabSpaceRelease','keyboardSpacePress','keyboardSpaceRelease','unknownKeyState',
 'imDeactivateEvents','imDoneEvents','grabReleaseRequests','imRequesterRequests']);
const invalid=()=>({valid:false,complete:false,before:[],after:[],delta:[]});
function readFixed(){
 const fd=openSync('/run/lab/server-route-witness.bin',constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
 try {
  const check=()=>{const s=fstatSync(fd);if(!s.isFile()||s.uid!==process.getuid()||
   (s.mode&0o7777)!==0o600||s.size!==80)throw Error('invalid fixed witness');};
  check();const out=[];
  for(let k=0;k<2;k++){const b=Buffer.alloc(80);let n=0;
   while(n<80){const got=readSync(fd,b,n,80-n,n);if(got===0)throw Error('short witness');n+=got;}
   out.push(b);}
  check();return out;
 }finally{closeSync(fd);}
}
export function parseServerSnapshots(pair){
 if(!Array.isArray(pair)||pair.length!==2||!pair.every(b=>Buffer.isBuffer(b)&&b.length===80)||!pair[0].equals(pair[1]))return null;
 const v=Array.from({length:20},(_,i)=>pair[0].readUInt32LE(i*4));
 if(v[0]!==0x52535731||v[1]!==1||(v[2]&1)!==0||v[3]!==1||v[4]!==1||v[5]!==1||
  v[17]!==1||v[18]!==0||v[19]!==0||v.slice(6,17).some(n=>n>=65535)||v[12]!==0)return null;
 return v.slice(6,17);
}
export function createServerRouteWitness({readSnapshots=readFixed}={}){
 let before=null,used=false,started=false;
 const read=async()=>{try{return parseServerSnapshots(await readSnapshots());}catch{return null;}};
 return Object.freeze({
  async begin(){if(started)return false;started=true;used=true;before=await read();
   if(before!==null&&!(before[0]>0&&before[2]>0&&before[4]>0))before=null;
   return before!==null;},
  async sample(){if(before===null||!used)return invalid();used=false;const after=await read();
   if(after===null||after.some((n,i)=>n<before[i]))return invalid();
   return {valid:true,complete:true,before:[...before],after,delta:after.map((n,i)=>n-before[i])};},
 });
}
