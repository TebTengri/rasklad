// SPDX-License-Identifier: MIT
// Rasklad OPTIONAL Space telemetry for the public focus stress TEST fixture only.
// Scope: case word_other_back_prose_space and the three owned fixture fields
// (prose, other, secret). This is instrumentation that can perturb timing. It is NOT a
// fix, NOT a readiness barrier and NOT a general keylogger.
//
// Records are bounded (max 32) and taken only while armed: keydown/keyup with
// e.code==='Space', and beforeinput/input, on any target in the fixture document. Each
// record holds only fixed typed data: type index, a target index derived solely from
// strict identity against the three captured field nodes (any other target = -1; the
// target's id/name/value are never read), isTrusted/isComposing/defaultPrevented
// booleans (defaultPrevented is the value seen at capture time only), a keyCodeIs229
// boolean, an inputType index from a fixed whitelist, and a value-class index for all
// three owned fields from a fixed list (unknown = -1). It never reads e.key or e.data and
// never stores text, type strings, key codes, selection or profile data. No synthetic
// events, text writes, polling, timers, filesystem, network, process access or imports.
//
// Listeners are capture-phase on the fixture's own document (not any host/global page),
// under one owned AbortController, so Space delivered to e.g. body is observed without any
// new read permissions. Events without Space code (including some IME Process events) are not recorded. Zero matching
// Space records cannot prove that no DOM delivery occurred.
//
// ARM is one-shot (started latch). SNAP is an atomic cut: in one evaluation it disarms,
// latches started, validates the fixed record shape and copies; no later events are
// collected. Malformed state yields null after the cut. complete=(dropped===0) flags
// truncation; valid is separate. stop() aborts and deletes only this module's own trace.
// This does not defend against hostile page script.

const CASE_ID = 'word_other_back_prose_space';
const J = JSON.stringify;
const T = J(CASE_ID);
const IDS = `['prose','other','secret']`;
const MAX_RECORDS = 32;
const DROP_CAP = 1000;
const INPUT_TYPES = Object.freeze(['insertText', 'insertCompositionText', 'deleteCompositionText',
  'insertFromComposition', 'insertReplacementText', 'deleteContentBackward', 'deleteContentForward']);
const VALUES = Object.freeze(['', ' ', 'ghbdtn', 'ghbdtn ',
  '\u043f\u0440\u0438\u0432\u0435\u0442', '\u043f\u0440\u0438\u0432\u0435\u0442 ']);

export const SPACE_TELEMETRY_CASE_ID = CASE_ID;

// Validates own state + own controller token and captured-element identity.
const PRE = `const W=window,s=W.__raskladSpaceTelemetry,c=W.__raskladSpaceTelemetryCtl;
 const live=!!s&&!!c&&s.token===${T}&&c.token===${T}&&typeof c.owns==='function'&&c.owns(s)===true&&
  !!s.els&&Array.isArray(s.records)&&${IDS}.every(id=>!!s.els[id]&&s.els[id]===document.getElementById(id));`;

const INSTALL = `(()=>{const W=window,stale=W.__raskladSpaceTelemetryCtl;
 if(stale!==undefined||W.__raskladSpaceTelemetry!==undefined){
  try{if(stale&&typeof stale.abort==='function')stale.abort();}
  finally{delete W.__raskladSpaceTelemetryCtl;delete W.__raskladSpaceTelemetry;}return false;}
 const ids=${IDS},els={};for(const id of ids){const el=document.getElementById(id);if(!el)return false;els[id]=el;}
 const ac=new AbortController();
 const IT=${J(INPUT_TYPES)},VC=${J(VALUES)};
 const s={token:${T},armed:false,started:false,records:[],dropped:0,els};
 const tix=t=>t===els.prose?0:t===els.other?1:t===els.secret?2:-1;
 const cls=()=>ids.map(id=>VC.indexOf(els[id].value));
 const rec=(ti,fi,e,key)=>{
  if(s.armed!==true||ac.signal.aborted)return;
  if(s.records.length>=${MAX_RECORDS}){if(s.dropped<${DROP_CAP})s.dropped+=1;return;}
  s.records.push({typeIndex:ti,targetIndex:fi,isTrusted:e.isTrusted===true,isComposing:e.isComposing===true,
   defaultPrevented:e.defaultPrevented===true,keyCodeIs229:key?e.keyCode===229:false,
   inputTypeIndex:key?-1:IT.indexOf(e.inputType),valueClassIndex:cls()});};
 const D=document,copt={capture:true,signal:ac.signal};
 D.addEventListener('keydown',e=>{if(s.armed===true&&e.code==='Space')rec(0,tix(e.target),e,true);},copt);
 D.addEventListener('keyup',e=>{if(s.armed===true&&e.code==='Space')rec(1,tix(e.target),e,true);},copt);
 D.addEventListener('beforeinput',e=>{if(s.armed===true)rec(2,tix(e.target),e,false);},copt);
 D.addEventListener('input',e=>{if(s.armed===true)rec(3,tix(e.target),e,false);},copt);
 W.__raskladSpaceTelemetryCtl=Object.freeze({token:${T},abort:()=>ac.abort(),owns:x=>x===s&&!ac.signal.aborted});
 W.__raskladSpaceTelemetry=s;return true;})()`;

// Activates only once, only with no prior data, and only after focus is back on prose.
const ARM = `(()=>{${PRE}if(!live||s.started!==false||s.armed!==false||s.records.length!==0||s.dropped!==0)return false;
 if(document.activeElement!==s.els.prose)return false;s.started=true;s.armed=true;return true;})()`;

// Atomic cut: disarm + latch first, then validate the fixed shape, then copy.
const SNAP = `(()=>{${PRE}if(!live)return null;
 const was=s.armed===true;s.armed=false;s.started=true;
 const B=x=>x===true||x===false,I=(x,l,h)=>Number.isInteger(x)&&x>=l&&x<=h;
 const R=s.records,d=s.dropped;
 if(!Array.isArray(R)||R.length>${MAX_RECORDS}||!I(d,0,${DROP_CAP})||(d>0&&R.length!==${MAX_RECORDS}))return null;
 const out=[];for(const r of R){if(!r||typeof r!=='object')return null;
  const v=r.valueClassIndex;
  if(!I(r.typeIndex,0,3)||!I(r.targetIndex,-1,2)||!B(r.isTrusted)||!B(r.isComposing)||
   !B(r.defaultPrevented)||!B(r.keyCodeIs229)||!I(r.inputTypeIndex,-1,${INPUT_TYPES.length - 1})||
   !Array.isArray(v)||v.length!==3||!v.every(x=>I(x,-1,${VALUES.length - 1})))return null;
  if(r.typeIndex<=1?r.inputTypeIndex!==-1:r.keyCodeIs229!==false)return null;
  out.push({typeIndex:r.typeIndex,targetIndex:r.targetIndex,isTrusted:r.isTrusted,isComposing:r.isComposing,
   defaultPrevented:r.defaultPrevented,keyCodeIs229:r.keyCodeIs229,inputTypeIndex:r.inputTypeIndex,
   valueClassIndex:[v[0],v[1],v[2]]});}
 return {wasArmedAtCut:was,dropped:d,records:out};})()`;

// Aborts and deletes only this module's own trace (matching token).
const STOP = `(()=>{const W=window,s=W.__raskladSpaceTelemetry,c=W.__raskladSpaceTelemetryCtl;
 if(!c||c.token!==${T}||typeof c.abort!=='function')return false;
 if(s!==undefined&&(!s||typeof s!=='object'||s.token!==${T}))return false;
 let ok=true;try{c.abort();}catch(_){ok=false;}
 finally{if(s&&typeof s==='object')s.armed=false;delete W.__raskladSpaceTelemetryCtl;delete W.__raskladSpaceTelemetry;}
 return ok&&W.__raskladSpaceTelemetryCtl===undefined&&W.__raskladSpaceTelemetry===undefined;})()`;

const isBool = v => v === true || v === false;
const isIdx = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
const invalid = () => ({ valid: false, armed: false, records: [], dropped: 0, complete: false });

function sanitizeRecord(r) {
  if (!r || typeof r !== 'object') return null;
  const v = r.valueClassIndex;
  if (!isIdx(r.typeIndex, 0, 3) || !isIdx(r.targetIndex, -1, 2) || !isBool(r.isTrusted) ||
    !isBool(r.isComposing) || !isBool(r.defaultPrevented) || !isBool(r.keyCodeIs229) ||
    !isIdx(r.inputTypeIndex, -1, INPUT_TYPES.length - 1) || !Array.isArray(v) || v.length !== 3 ||
    !v.every(x => isIdx(x, -1, VALUES.length - 1))) return null;
  const key = r.typeIndex <= 1;
  if (key ? r.inputTypeIndex !== -1 : r.keyCodeIs229 !== false) return null;
  return {
    typeIndex: r.typeIndex,
    targetIndex: r.targetIndex,
    isTrusted: r.isTrusted,
    isComposing: r.isComposing,
    defaultPrevented: r.defaultPrevented,
    keyCodeIs229: r.keyCodeIs229,
    inputTypeIndex: r.inputTypeIndex,
    valueClassIndex: [v[0], v[1], v[2]],
  };
}

function sanitizeSnapshot(raw) {
  if (!raw || typeof raw !== 'object' || !isBool(raw.wasArmedAtCut) || !isIdx(raw.dropped, 0, DROP_CAP) ||
    !Array.isArray(raw.records) || raw.records.length > MAX_RECORDS) return invalid();
  if (raw.dropped > 0 && raw.records.length !== MAX_RECORDS) return invalid();
  const records = [];
  for (const r of raw.records) {
    const c = sanitizeRecord(r);
    if (c === null) return invalid();
    records.push(c);
  }
  // armed = armed state at the atomic cut; complete flags truncation, independent of valid.
  return { valid: true, armed: raw.wasArmedAtCut, records, dropped: raw.dropped, complete: raw.dropped === 0 };
}

export function createSpaceTelemetry(options) {
  const ev = options !== null && typeof options === 'object' && typeof options.ev === 'function' ? options.ev : null;
  // ev failures collapse to a fixed failure result; no raw exception text escapes.
  const run = async expression => {
    if (ev === null) return { ok: false, value: null };
    try {
      return { ok: true, value: await ev(expression) };
    } catch {
      return { ok: false, value: null };
    }
  };
  const bool = async expression => {
    const r = await run(expression);
    return r.ok === true && r.value === true;
  };
  return Object.freeze({
    async begin(caseId) {
      if (caseId !== CASE_ID) return false;
      return bool(INSTALL);
    },
    async arm() {
      return bool(ARM);
    },
    async snapshot() {
      const r = await run(SNAP);
      return r.ok === true ? sanitizeSnapshot(r.value) : invalid();
    },
    async stop() {
      return bool(STOP);
    },
  });
}
