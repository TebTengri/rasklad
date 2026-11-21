// SPDX-License-Identifier: MIT
// Closed synthetic fixture only. Read-only fixed counters, never input payload.
// Default schema 2: 22 counters / 24 words. Explicit coreEntry schema 3:
// own 22 counters + shim markers + core 10 counters / 36 words. 255 means saturation.
// noQuery=true is the zero-GetStats control: no witness object, query never called.
export const WITNESS_COUNTERS=Object.freeze([
 'spaceCodePressSeen','spaceCodeReleaseSeen','normalizedSpacePress','priorFilteredPress',
 'focusedPress','eligiblePress','bufferNonemptyPress','bypassPress','passIneligible',
 'passBypass','passEmpty','commitSpaceBranch','flushOriginalSpaceCode','commitStringCalls',
 'focusIn','focusOut','reset','capabilityChange','noStatePress','releaseFiltered',
 'allKeyPressSeen','allKeyReleaseSeen',
]);
export const CORE_ENTRY_COUNTERS=Object.freeze([
 'coreAllPressSeen','coreAllReleaseSeen','coreSpacePressSeen','coreSpaceReleaseSeen',
 'coreSpacePressFocused','coreSpacePressUnfocused','coreSpacePressReturnedTrue',
 'coreSpacePressReturnedFalse','coreSpaceReleaseReturnedTrue','coreSpaceReleaseReturnedFalse',
]);
export const GRAB_ENTRY_COUNTERS=Object.freeze(['grabRegistrations','grabAllPressSeen','grabAllReleaseSeen',
 'grabSpacePressSeen','grabSpaceReleaseSeen','grabOtherStateSeen']);
const invalid=()=>({valid:false,complete:false,before:[],after:[],delta:[]});
export function parseWitnessReply(raw,coreEntry=false,grabEntry=false){
 if(typeof raw!=='string'||raw.length>768||!/^\(\[\s*(?:uint32 )?\d+(?:,\s*(?:uint32 )?\d+)*\s*\],\)\s*$/.test(raw))return null;
 const values=raw.replaceAll('uint32 ','').match(/\d+/g).map(Number);
 if(!values.every(x=>Number.isInteger(x)&&x>=0&&x<=255)||values[1]!==1)return null;
 if(grabEntry===true){
  if(values.length!==43||values[0]!==4||values[24]!==2||values[25]!==1||values[36]!==1)return null;
  return [...values.slice(2,24),...values.slice(26,36),...values.slice(37)];
 }
 if(coreEntry===true){
  if(values.length!==36||values[0]!==3||values[24]!==1||values[25]!==1)return null;
  return [...values.slice(2,24),...values.slice(26)];
 }
 if(values.length!==24||values[0]!==2)return null;
 return values.slice(2);
}
export function createAddonWitness(options){
 if(options.noQuery===true)return null;
 const {query,coreEntry=false,grabEntry=false}=options;
 let before=null,used=false;
 const read=async()=>{try{return parseWitnessReply(await query(),coreEntry,grabEntry);}catch{return null;}};
 return Object.freeze({
  async begin(){if(before!==null||used)return false;used=true;before=await read();
   // Prior baseline typing must have reached both observer layers in core mode.
   if((coreEntry===true||grabEntry===true)&&before!==null&&!(before[20]>0&&before[22]>0))before=null;
   if(grabEntry===true&&before!==null&&!(before[32]>0&&before[33]>0))before=null;
   return before!==null;},
  async sample(){
   if(before===null||!used)return invalid();used=false;
   const after=await read();if(after===null||after.some((x,i)=>x<before[i]))return invalid();
   const complete=before.every((x,i)=>x<255&&after[i]<255)
    && (!grabEntry || after[37]===before[37]);
   return {valid:true,complete,before:[...before],after,delta:after.map((x,i)=>x===255||before[i]===255?-1:x-before[i])};
  },
 });
}
