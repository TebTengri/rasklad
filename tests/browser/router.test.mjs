// SPDX-License-Identifier: MIT
// Router binding tests. Synthetic evidence and fictional tokens only; no corpus accuracy claims.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
execFileSync(path.join(here, '..', '..', 'bin', 'build-browser-fixture'), [], { cwd: root, stdio: ['ignore', 'ignore', 'inherit'] });

const require = createRequire(import.meta.url);
const unwrap = (mod, name) => (mod && mod[name]) || mod;
const policy = unwrap(require(path.join(root, 'build', 'browser-fixture', 'policy.js')), 'RaskladPolicyData');
const Converter = unwrap(require(path.join(root, 'build', 'browser-fixture', 'converter.js')), 'RaskladConverter');
const { createRouter } = require(path.join(root, 'browser', 'router.js'));

const convert = (text, direction) => Converter.convert(text, direction);
const US = 'qzxv'; // fictional, not a word
const RU = 'ЙЯЧМ'; // fictional, capitalized RU on the same physical keys
const clone = () => JSON.parse(JSON.stringify(policy));
const prose = (token, extra = {}) => ({ field: 'prose', layout: 'us', profile: 'safeConfirmed', token, ...extra });
const abst = (reason, evidenceQueried) => ({ action: 'abstain', reason, evidenceQueried, candidate: '' });
const pair = (bits, shift) => ({ observed: Boolean(bits & (1 << shift)), alternative: Boolean(bits & (2 << shift)) });
const synthetic = (bits, model) => ({ full: pair(bits, 0), control: pair(bits, 2), stem: pair(bits, 4), model });
function forced(action, reason) {
  const p = clone();
  p.outcomes = [{ action, reason }];
  p.table = new Array(512).fill(0);
  return p;
}
function spy(impl) {
  const fn = (...args) => { fn.calls.push(args); return impl(...args); };
  fn.calls = [];
  return fn;
}
function expectedRow(p, m, bits) {
  const out = policy.outcomes[policy.table[(p * 4 + m) * 64 + bits]];
  const converts = out.action === 'correct' || out.action === 'suggest';
  return { action: out.action, reason: out.reason, evidenceQueried: true, candidate: converts ? Converter.convert(US, 'us-ru') : '' };
}

test('fixture shape and fictional tokens are eligible', () => {
  assert.equal(policy.table.length, 512);
  assert.ok(US.length >= policy.minKeys && US.length <= policy.maxKeys);
  for (const ch of US) assert.ok(Object.hasOwn(policy.canonical.us, ch));
  for (const ch of RU) assert.ok(Object.hasOwn(policy.canonical.ru, ch));
});

test('exhaustive generated table: 2 profiles x 4 models x 64 flag rows', () => {
  let current = null;
  const router = createRouter({ policy, convert, evidence: () => current });
  for (let p = 0; p < 2; p++) {
    for (let m = 0; m < 4; m++) {
      for (let bits = 0; bits < 64; bits++) {
        current = synthetic(bits, m === 3 ? 'notAModel' : policy.models[m]);
        const want = expectedRow(p, m, bits);
        assert.deepEqual(router.route(prose(US, { profile: policy.profiles[p] })), want, `p=${p} m=${m} bits=${bits}`);
        if (p === 0) assert.equal(router.decideUs(US), want.action === 'correct' ? want.candidate : null);
      }
    }
  }
});

test('decideUs returns only Correct candidates; Suggest never auto-changes', () => {
  const ev = () => synthetic(0, 'keep');
  const candidate = Converter.convert(US, 'us-ru');
  assert.equal(createRouter({ policy: forced('correct', 'dictionaryOnly'), convert, evidence: ev }).decideUs(US), candidate);
  const s = createRouter({ policy: forced('suggest', 'modelOnly'), convert, evidence: ev });
  assert.deepEqual(s.route(prose(US)), { action: 'suggest', reason: 'modelOnly', evidenceQueried: true, candidate });
  assert.equal(s.decideUs(US), null);
  const k = createRouter({ policy: forced('keep', 'observedWordKnown'), convert: () => assert.fail('converted'), evidence: ev });
  assert.deepEqual(k.route(prose(US)), { action: 'keep', reason: 'observedWordKnown', evidenceQueried: true, candidate: '' });
  assert.equal(k.decideUs(US), null);
});

test('context gates run before token read and evidence', () => {
  const ev = spy(() => synthetic(0, 'keep'));
  const router = createRouter({ policy, convert, evidence: ev });
  const req = (field, layout, profile) => ({ field, layout, profile, get token() { throw new Error('token read'); } });
  const cases = [
    [req('protected', 'unknown', 'bogus'), 'keep', 'protectedField'],
    [req('unknown', 'us', 'safeConfirmed'), 'abstain', 'unknownContext'],
    [req(undefined, 'us', 'safeConfirmed'), 'abstain', 'unknownContext'],
    [req('prose', 'unknown', 'safeConfirmed'), 'abstain', 'unknownContext'],
    [req('prose', 'us', 'bogus'), 'abstain', 'invalidRequest'],
    [req('prose', 'ru', '__proto__'), 'abstain', 'invalidRequest'],
  ];
  for (const [request, action, reason] of cases) {
    assert.deepEqual(router.route(request), { action, reason, evidenceQueried: false, candidate: '' }, reason);
  }
  assert.deepEqual(router.route(req('prose', 'us', 'safeConfirmed')), abst('invalidRequest', false));
  assert.deepEqual(router.route(null), abst('invalidRequest', false));
  assert.equal(ev.calls.length, 0);
});

test('token bounds, unicode and punctuation are gated before evidence', () => {
  const ev = spy(() => synthetic(0, 'keep'));
  const router = createRouter({ policy, convert, evidence: ev });
  const gate = (token, reason, layout = 'us') => assert.deepEqual(router.route(prose(token, { layout })), abst(reason, false), reason);
  gate('q'.repeat(policy.maxKeys + 1), 'tooLong');
  gate('q'.repeat(1000000), 'tooLong');
  gate('', 'unsupportedToken');
  gate('qz' + String.fromCodePoint(0x1F600), 'unsupportedToken');
  gate('qzx' + String.fromCharCode(0xD800), 'unsupportedToken');
  for (const ch of ['!', '1', ' ', '-', 'é', String.fromCharCode(0)]) {
    if (!Object.hasOwn(policy.canonical.us, ch)) gate('qzx' + ch, 'unsupportedToken');
  }
  if (![...'йячм'].some((c) => Object.hasOwn(policy.canonical.us, c))) gate('йячм', 'unsupportedToken');
  if (![...US].some((c) => Object.hasOwn(policy.canonical.ru, c))) gate(US, 'unsupportedToken', 'ru');
  if (policy.minKeys > 1) gate(US.slice(0, policy.minKeys - 1), 'tooShort');
  for (const bad of [new String(US), { toString: () => US }, 42, null, undefined, [US]]) {
    assert.deepEqual(router.route(prose(bad)), abst('invalidRequest', false));
  }
  assert.equal(router.decideUs({ toString: () => US }), null);
  assert.equal(ev.calls.length, 0);
  const punct = [',', '.', ';', '[', ']', '`', String.fromCharCode(39)].filter((ch) => Object.hasOwn(policy.canonical.us, ch));
  for (const ch of punct) assert.equal(router.route(prose('qzx' + ch)).evidenceQueried, true);
  assert.equal(ev.calls.length, punct.length);
});

test('evidence query is frozen, transient and keyed by canonical map', () => {
  const ev = spy(() => synthetic(0, 'keep'));
  const router = createRouter({ policy, convert, evidence: ev });
  router.route(prose(US));
  router.route(prose(US));
  const [q] = ev.calls[0];
  assert.deepEqual(Object.keys(q).sort(), ['keys', 'layout', 'token']);
  assert.ok(Object.isFrozen(q) && Object.isFrozen(q.keys));
  assert.deepEqual(q.keys, [...US].map((c) => policy.canonical.us[c]));
  assert.notEqual(ev.calls[1][0].keys, q.keys);
});

test('capitalized RU converts back to US', () => {
  const ev = spy(() => synthetic(0, 'correct'));
  const router = createRouter({ policy: forced('correct', 'dictionaryAndModelAgree'), convert, evidence: ev });
  const r = router.route(prose(RU, { layout: 'ru' }));
  assert.equal(r.candidate, Converter.convert(RU, 'ru-us'));
  assert.equal(r.candidate, 'QZXV');
  assert.equal(ev.calls[0][0].layout, 'ru');
  assert.deepEqual(ev.calls[0][0].keys, [...RU].map((c) => policy.canonical.ru[c]));
});

test('evidence null/throw/promise/bad fields fail closed', () => {
  const run = (impl) => createRouter({ policy, convert, evidence: impl }).route(prose(US));
  const good = synthetic(0, 'keep');
  assert.deepEqual(createRouter({ policy, convert }).route(prose(US)), abst('evidenceUnavailable', false));
  assert.deepEqual(run(() => null), abst('evidenceUnavailable', true));
  assert.deepEqual(run(() => { throw new Error('offline'); }), abst('evidenceUnavailable', true));
  assert.deepEqual(run(() => Promise.resolve(good)), abst('invalidEvidence', true));
  assert.deepEqual(run(() => ({ ...good, then() {} })), abst('invalidEvidence', true));
  const bads = [undefined, 0, 'keep', true, () => good, Object.create(good),
    { ...good, full: null }, { ...good, control: { observed: 1, alternative: false } },
    { ...good, stem: { observed: false, alternative: 'false' } }, { ...good, full: { observed: true } },
    Object.defineProperty({ ...good }, 'model', { get() { throw new Error('x'); } }),
    Object.defineProperty({ ...good }, 'stem', { get() { throw new Error('x'); } })];
  for (const bad of bads) assert.deepEqual(run(() => bad), abst('invalidEvidence', true));
  for (const model of ['invalid', 'notAModel', 7, null, undefined, {}, 'Keep']) {
    assert.deepEqual(run(() => synthetic(5, model)), expectedRow(0, 3, 5));
  }
});

test('malformed policy fails closed at factory time', () => {
  assert.doesNotThrow(() => createRouter({ policy: clone(), convert }));
  const mutations = [
    (p) => { p.schema = 2; }, (p) => { p.minKeys = 0; }, (p) => { p.minKeys = p.maxKeys + 1; },
    (p) => { p.maxKeys = 1e9; }, (p) => { p.maxKeys = '32'; },
    (p) => { p.profiles = [...p.profiles].reverse(); }, (p) => { p.models = p.models.slice(0, 3); },
    (p) => { p.table = p.table.slice(1); }, (p) => { p.table = new Array(512); },
    (p) => { p.table[0] = p.outcomes.length; }, (p) => { p.table[5] = -1; }, (p) => { p.table[7] = '0'; }, (p) => { p.table[9] = 0.5; },
    (p) => { p.outcomes[0] = { action: 'explode', reason: p.outcomes[0].reason }; },
    (p) => { p.outcomes[0].reason = 'madeUp'; }, (p) => { p.outcomes[0] = null; }, (p) => { p.outcomes = []; },
    (p) => { p.canonical.us = {}; }, (p) => { p.canonical.us.ab = 1; }, (p) => { p.canonical.ru['й'] = 33; },
    (p) => { p.canonical.us.q = -1; }, (p) => { p.canonical.us.q = '1'; }, (p) => { delete p.canonical.ru; },
  ];
  mutations.forEach((mutate, i) => {
    const p = clone();
    mutate(p);
    assert.throws(() => createRouter({ policy: p, convert }), TypeError, `mutation ${i}`);
  });
  assert.throws(() => createRouter(), TypeError);
  assert.throws(() => createRouter({ policy: null, convert }), TypeError);
  assert.throws(() => createRouter({ policy, convert: null }), TypeError);
  assert.throws(() => createRouter({ policy, convert, evidence: 'remote' }), TypeError);
});

test('char lookups use own properties only', () => {
  const p = clone();
  p.canonical.us = Object.assign(Object.create({ '§': 1 }), p.canonical.us);
  const ev = spy(() => synthetic(0, 'keep'));
  Object.prototype['¤'] = 2;
  try {
    const router = createRouter({ policy: p, convert, evidence: ev });
    assert.deepEqual(router.route(prose('qzx§')), abst('unsupportedToken', false));
    assert.deepEqual(router.route(prose('qzx¤')), abst('unsupportedToken', false));
  } finally {
    delete Object.prototype['¤'];
  }
  assert.equal(ev.calls.length, 0);
});

test('policy is copied; router frozen; results fresh', () => {
  const p = forced('correct', 'dictionaryOnly');
  const router = createRouter({ policy: p, convert, evidence: () => synthetic(0, 'keep') });
  p.outcomes[0].action = 'keep';
  p.table.fill(99);
  p.canonical.us = {};
  p.maxKeys = 1;
  const a = router.route(prose(US));
  assert.equal(a.action, 'correct');
  assert.ok(Object.isFrozen(router));
  a.candidate = 'mutated';
  const b = router.route(prose(US));
  assert.notEqual(a, b);
  assert.equal(b.candidate, Converter.convert(US, 'us-ru'));
});

test('malicious converter fails closed', () => {
  const p = forced('correct', 'dictionaryOnly');
  const ev = () => synthetic(0, 'keep');
  const bads = [() => 'x', () => US + 'y', () => ({ toString: () => 'abcd', length: 4 }), () => new String('abcd'),
    () => null, () => 1234, () => { throw new Error('boom'); }];
  for (const impl of bads) {
    const router = createRouter({ policy: p, convert: impl, evidence: ev });
    assert.deepEqual(router.route(prose(US)), abst('invalidEvidence', true));
    assert.equal(router.decideUs(US), null);
  }
  const c = spy(convert);
  createRouter({ policy: p, convert: c, evidence: ev }).route(prose(RU, { layout: 'ru' }));
  assert.deepEqual(c.calls, [[RU, 'ru-us']]);
});
