// SPDX-License-Identifier: MIT
// Closed synthetic fixture only. Read-only fixed counters, never input payload.
// Counter order is the versioned C++ witness contract. 255 means saturation.
export const WITNESS_COUNTERS=Object.freeze([
 'spaceCodePressSeen','spaceCodeReleaseSeen','normalizedSpacePress','priorFilteredPress',
 'focusedPress','eligiblePress','bufferNonemptyPress','bypassPress','passIneligible',
 'passBypass','passEmpty','commitSpaceBranch','flushOriginalSpaceCode','commitStringCalls',
 'focusIn','focusOut','reset','capabilityChange','noStatePress','releaseFiltered',
]);
const invalid=()=>({valid:false,complete:false,before:[],after:[],delta:[]});
export function parseWitnessReply(raw){
 if(typeof raw!=='string'||raw.length>768||!/^\(\[\s*(?:uint32 )?\d+(?:,\s*(?:uint32 )?\d+)*\s*\],\)\s*$/.test(raw))return null;
 const values=raw.replaceAll('uint32 ','').match(/\d+/g).map(Number);
 if(values.length!==22||values[0]!==1||values[1]!==1||!values.every(x=>Number.isInteger(x)&&x>=0&&x<=255))return null;
 return values.slice(2);
}
export function createAddonWitness({query}){
 let before=null,used=false;
 const read=async()=>{try{return parseWitnessReply(await query());}catch{return null;}};
 return Object.freeze({
  async begin(){if(before!==null||used)return false;used=true;before=await read();return before!==null;},
  async sample(){
   if(before===null||!used)return invalid();used=false;
   const after=await read();if(after===null||after.some((x,i)=>x<before[i]))return invalid();
   const complete=before.every((x,i)=>x<255&&after[i]<255);
   return {valid:true,complete,before:[...before],after,delta:after.map((x,i)=>x===255||before[i]===255?-1:x-before[i])};
  },
 });
}
