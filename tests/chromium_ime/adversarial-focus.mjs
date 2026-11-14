// SPDX-License-Identifier: MIT
// Rasklad focus stress probe: DOM intra-surface only (one Chromium page, three owned
// fixture fields). It does NOT cover stale Wayland text-input serials or cross-surface
// focus, and is not a protocol-serial proof. Strict oracle: a missing or duplicated
// word/delimiter is a failure, and platform differences are reported as failures.
//
// Page state is an owned test fixture keyed by a fixed per-case token. Listener cleanup
// lives in a separate closure controller (AbortController), so it still works if the
// primary state object disappears. This does not defend against hostile page script.
//
// End-event policy (TEST provenance only, NOT authentication or security):
//  - 'trusted_only' (default, strict): every counted compositionstart/compositionend
//    must have isTrusted===true.
//  - 'controlled_chromium_scoped_end': compositionstart still requires isTrusted===true.
//    A source-target compositionend with isTrusted===false is also counted, but only on
//    a captured field that already recorded a trusted compositionstart. This matches the
//    observed Chromium 155.0.8059.84 InputMethodController behaviour (scoped dispatch for
//    end, normal dispatch for start). The caller may select it ONLY after an exact
//    Chromium 155 version check and a real native VK Space positive control in this
//    CLOSED owned fixture (reviewed: no JS-generated composition events). It does not
//    authenticate native events for arbitrary pages or applications. Duplicate ends are
//    still recorded, so exact start/end balance and hook-count oracles reject them.
//
// Native cleanup runs after the final snapshot and listener abort: reset to the known
// owned 'other' field, a real Space to clear any add-on Escape bypass, then reset.
//
// Authored by Claude Opus; Codex integrated and tightened identity/final-code guards
// after independent review, including cleanup postconditions and dependent-case gating. The baseline original-word gate is enforced in driver.mjs.

const PROSE_WORD = 'ghbdtn';
const RU_WORD_SPACE = '\u043f\u0440\u0438\u0432\u0435\u0442 ';
const HITS_CAP = 3;
const COUNT_CAP = 1000;
const BARRIER_MS = 1000;
const IDS = "['prose','other','secret']";
const J = JSON.stringify;

export const END_POLICY_TRUSTED_ONLY = 'trusted_only';
export const END_POLICY_SCOPED = 'controlled_chromium_scoped_end';

// Validates owned state + controller token, fixed end policy and captured-element identity.
const pre = K => `const s=window.__raskladFocus,c=window.__raskladFocusCtl;
 const live=!!s&&!!c&&s.token===${K.T}&&c.token===${K.T}&&s.scopedEnd===${K.S}&&
  typeof c.owns==='function'&&c.owns(s)===true&&!!s.els&&
  ${IDS}.every(id=>!!s.els[id]&&s.els[id]===document.getElementById(id));`;

const install = (K, hook) => `(()=>{const W=window,stale=W.__raskladFocusCtl;
 if(stale!==undefined||W.__raskladFocus!==undefined){
  try{if(stale&&typeof stale.abort==='function')stale.abort();}
  finally{delete W.__raskladFocusCtl;delete W.__raskladFocus;}return false;}
 const els={};for(const id of ${IDS}){const el=document.getElementById(id);if(!el)return false;els[id]=el;}
 const ac=new AbortController(),opt={signal:ac.signal},scoped=${K.S};
 const s={token:${K.T},hook:${hook ? 'true' : 'false'},scopedEnd:scoped,armed:false,hits:0,endMark:-1,startMark:-1,els,
  starts:{prose:0,other:0,secret:0},ends:{prose:0,other:0,secret:0}};
 const startOk=(e,el)=>e.target===el&&e.isTrusted===true;
 const endOk=(e,el,id)=>e.target===el&&(e.isTrusted===true||
  (scoped===true&&e.isTrusted===false&&Number.isInteger(s.starts[id])&&s.starts[id]>0));
 const inc=n=>Math.min(n+1,${COUNT_CAP});
 for(const id of ${IDS}){const el=els[id];
  el.addEventListener('compositionstart',e=>{if(startOk(e,el))s.starts[id]=inc(s.starts[id]);},opt);
  el.addEventListener('compositionend',e=>{if(endOk(e,el,id))s.ends[id]=inc(s.ends[id]);},opt);}
 if(s.hook)els.prose.addEventListener('compositionend',e=>{
  if(!endOk(e,els.prose,'prose')||!s.armed)return;
  s.hits=Math.min(s.hits+1,${HITS_CAP});
  if(s.hits===1)els.other.focus();},opt);
 W.__raskladFocusCtl=Object.freeze({token:${K.T},abort:()=>ac.abort(),owns:candidate=>candidate===s&&!ac.signal.aborted});
 W.__raskladFocus=s;return true;})()`;

const CLEAR = `(()=>{const W=window,c=W.__raskladFocusCtl,s=W.__raskladFocus;let ok=true;
 try{if(c&&typeof c.abort==='function')c.abort();else if(c!==undefined||s!==undefined)ok=false;}
 catch(_){ok=false;}
 finally{if(s&&typeof s==='object')s.armed=false;delete W.__raskladFocusCtl;delete W.__raskladFocus;}
 return ok&&W.__raskladFocusCtl===undefined&&W.__raskladFocus===undefined;})()`;

// Precondition: trusted source composition active in prose holding the raw word.
const sourceActive = K => `(()=>{${pre(K)}if(!live)return false;const p=s.els.prose;
 return s.starts.prose>s.ends.prose&&p.value===${J(PROSE_WORD)}&&document.activeElement===p;})()`;

const markEnd = K => `(()=>{${pre(K)}if(!live)return false;s.endMark=s.ends.prose;return true;})()`;
const markStart = K => `(()=>{${pre(K)}if(!live)return false;s.startMark=s.starts.prose;return true;})()`;
const arm = K => `(()=>{${pre(K)}if(!live||!s.hook||s.armed||s.hits!==0)return false;s.armed=true;return true;})()`;

const focusTo = (K, id) => `(()=>{${pre(K)}if(!live)return false;const el=s.els[${J(id)}];
 el.focus();return document.activeElement===el;})()`;

const held = (K, id) => `(()=>{${pre(K)}if(!live)return false;
 return s.els.prose.value===${J(PROSE_WORD)}&&document.activeElement===s.els[${J(id)}];})()`;

// Bounded event-state barrier: resolves true once an accepted prose compositionend has
// arrived after endMark and the source composition is inactive. Counters were
// registered at install (before the action), so a synchronous event resolves at once.
// Acceptance uses the same fixed policy as the install-time counters.
const sourceEnded = K => `new Promise(res=>{${pre(K)}
 if(!live||s.endMark<0){res(false);return;}
 const p=s.els.prose,scoped=${K.S};let timer=null,fn=null,settled=false;
 const done=()=>s.ends.prose>s.endMark&&s.ends.prose===s.starts.prose;
 const fin=v=>{if(settled)return;settled=true;
  try{if(timer!==null)clearTimeout(timer);if(fn)p.removeEventListener('compositionend',fn);}
  finally{timer=null;fn=null;res(v);}};
 if(done()){fin(true);return;}
 fn=e=>{if(e.target===p&&(e.isTrusted===true||
  (scoped===true&&e.isTrusted===false&&s.starts.prose>0))&&done())fin(true);};
 p.addEventListener('compositionend',fn);
 timer=setTimeout(()=>fin(false),${BARRIER_MS});})`;

// Single final snapshot. Returns an integer: -1 if state is absent/replaced, else
// (allChecksOk ? 100 : 0) + hookHits (hits capped at HITS_CAP).
const snapshot = (K, x) => `(()=>{${pre(K)}if(!live)return -1;
 const g=id=>s.els[id].value;
 const ok=g('prose')===${J(x.prose)}&&g('other')===${J(x.other)}&&g('secret')===${J(x.secret)}&&
  document.activeElement===s.els[${J(x.active)}]&&
  s.ends.prose>=1&&s.ends.prose>s.endMark&&s.ends.prose===s.starts.prose&&
  s.starts.secret===0&&s.ends.secret===0&&
  ${x.sinceMark ? 's.startMark>=0&&s.starts.prose===s.startMark' : 'true'};
 const h=s.hits;if(!Number.isInteger(h)||h<0||h>${HITS_CAP})return -1;
 return (ok?100:0)+h;})()`;

export async function runFocusScenarios({ ev, type, reset }, endEventPolicy = END_POLICY_TRUSTED_ONLY) {
  let policy = null;
  if (endEventPolicy === END_POLICY_TRUSTED_ONLY) policy = END_POLICY_TRUSTED_ONLY;
  else if (endEventPolicy === END_POLICY_SCOPED) policy = END_POLICY_SCOPED;
  if (policy === null) {
    return { passed: false, checks: 0, cases: [], endEventPolicy: null, errorCode: 'UNKNOWN_END_EVENT_POLICY' };
  }
  const S = policy === END_POLICY_SCOPED ? 'true' : 'false';

  const is = async expression => (await ev(expression)) === true;
  let setupClean = true;

  async function runCase(id, hook, body) {
    const r = { id, passed: false, precondition: false, hookHits: null, errorCode: null, executed: false, cleanupPassed: null };
    if (!setupClean) { r.errorCode = 'DEPENDENT_NOT_RUN'; return r; }
    r.executed = true;
    const K = { T: J(id), S };
    try {
      await reset();
      if (!(await is(install(K, hook)))) { r.errorCode = 'INSTALL_FAILED'; return r; }
      await type('word');
      r.precondition = await is(sourceActive(K));
      if (!r.precondition) { r.errorCode = 'PRECONDITION_FAILED'; return r; }
      const expect = await body(K);
      if (expect) {
        const code = await ev(snapshot(K, expect));
        if (Number.isInteger(code) && code >= 0) {
          r.hookHits = code % 100;
          r.passed = code === 100 + (hook ? 1 : 0);
        }
      }
      if (!r.passed) r.errorCode = 'ASSERTION_FAILED';
    } catch {
      r.passed = false;
      r.errorCode = 'CASE_ERROR';
    } finally {
      let cleanOk = true;
      try {
        if (!(await is(CLEAR))) cleanOk = false;
      } catch {
        cleanOk = false;
      }
      try {
        await reset('other');
        await type('space');
        if (!(await is(`(()=>{const g=id=>document.getElementById(id).value;
          return g('prose')===''&&g('other')===' '&&g('secret')===''&&
           Array.isArray(window.imeEvents)&&!imeEvents.some(e=>e.type==='compositionstart');})()`))) cleanOk = false;
        await reset();
      } catch {
        cleanOk = false;
      }
      r.cleanupPassed = cleanOk;
      if (!cleanOk) {
        setupClean = false;
        r.passed = false;
        if (!r.errorCode) r.errorCode = 'CLEANUP_ERROR';
      }
    }
    return r;
  }

  const cases = [];

  // 1: word preedit -> focus secret (source must end first) -> space
  cases.push(await runCase('word_focus_secret_space', false, async K => {
    if (!(await is(markEnd(K)))) return false;
    if (!(await is(focusTo(K, 'secret')))) return false;
    if (!(await is(sourceEnded(K)))) return false;
    if (!(await is(held(K, 'secret')))) return false;
    await type('space');
    return { prose: PROSE_WORD, other: '', secret: ' ', active: 'secret' };
  }));

  // 2: word preedit -> other focus (source must end first) -> prose focus -> space
  cases.push(await runCase('word_other_back_prose_space', false, async K => {
    if (!(await is(markEnd(K)))) return false;
    if (!(await is(focusTo(K, 'other')))) return false;
    if (!(await is(sourceEnded(K)))) return false;
    if (!(await is(held(K, 'other')))) return false;
    if (!(await is(focusTo(K, 'prose')))) return false;
    if (!(await is(held(K, 'prose')))) return false;
    if (!(await is(markStart(K)))) return false;
    await type('space');
    return { prose: PROSE_WORD + ' ', other: '', secret: '', active: 'prose', sinceMark: true };
  }));

  // 3: word preedit -> escape (must end source BEFORE any blur) -> other focus -> space
  cases.push(await runCase('word_escape_other_space', false, async K => {
    if (!(await is(markEnd(K)))) return false;
    await type('escape');
    if (!(await is(sourceEnded(K)))) return false;
    if (!(await is(held(K, 'prose')))) return false;
    if (!(await is(focusTo(K, 'other')))) return false;
    if (!(await is(held(K, 'other')))) return false;
    await type('space');
    return { prose: PROSE_WORD, other: ' ', secret: '', active: 'other' };
  }));

  // 4: word preedit -> space; accepted prose compositionend hook moves focus to other
  cases.push(await runCase('word_space_compositionend_hook_other', true, async K => {
    if (!(await is(arm(K)))) return false;
    if (!(await is(markEnd(K)))) return false;
    await type('space');
    if (!(await is(sourceEnded(K)))) return false;
    return { prose: RU_WORD_SPACE, other: '', secret: '', active: 'other' };
  }));

  return {
    passed: cases.length === 4 && cases.every(c => c.passed === true),
    checks: cases.length,
    executedCases: cases.filter(c => c.executed).length,
    cases,
    endEventPolicy: policy,
    provenance: 'test_fixture_only_not_authentication',
  };
}