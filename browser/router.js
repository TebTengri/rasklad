/* Project license pending; see README.md
 * rasklad browser router binding. Bounded glue between the page adapter, the
 * generated policy table and a caller-supplied synchronous evidence provider.
 * It contains no linguistic rules: every evidence outcome is looked up in the
 * table exported by the native DecisionRouter. No caches, logs, timers or I/O.
 * The page adapter must still authorize the real field before calling decideUs. */
(function (root, factory) {
  'use strict';
  const api = factory();
  if (typeof module === 'object' && module !== null && module.exports) module.exports = api;
  else if (root) root.RaskladRouter = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const ACTIONS = ['keep', 'correct', 'suggest', 'abstain'];
  const REASONS = ['protectedField', 'unknownContext', 'invalidRequest', 'unsupportedToken', 'tooShort', 'tooLong',
    'evidenceUnavailable', 'invalidEvidence', 'observedWordKnown', 'sharedAmbiguity', 'dictionaryAndModelAgree',
    'dictionaryOnly', 'modelOnly', 'noEvidence'];
  const PROFILES = ['safeConfirmed', 'requestedHybrid'];
  const MODELS = ['keep', 'correct', 'abstain', 'invalid'];
  const INVALID_MODEL = 3;
  const PAIRS = ['full', 'control', 'stem'];
  const TABLE_SIZE = PROFILES.length * MODELS.length * 64;
  const KEY_INDEX_MAX = 32;
  const MAX_KEYS_LIMIT = 256;
  const MAX_CANONICAL_CHARS = 1024;
  const MAX_OUTCOMES = 256;
  const DIRECTION = Object.freeze({ us: 'us-ru', ru: 'ru-us' });
  const apply = Reflect.apply;

  const hasOwn = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
  const own = (o, k) => (hasOwn(o, k) ? o[k] : undefined);
  const isObj = (v) => typeof v === 'object' && v !== null;
  function fail(what) { throw new TypeError('RaskladRouter: malformed ' + what); }

  function sameList(list, expected) {
    if (!Array.isArray(list) || list.length !== expected.length) return false;
    for (let i = 0; i < expected.length; i++) if (list[i] !== expected[i]) return false;
    return true;
  }

  // Copies own enumerable single-unit keys into a private Map (no prototype lookups).
  function compileCanonical(map, name) {
    if (!isObj(map) || Array.isArray(map)) fail('canonical.' + name);
    const chars = Object.keys(map);
    if (chars.length === 0 || chars.length > MAX_CANONICAL_CHARS) fail('canonical.' + name);
    const out = new Map();
    for (const ch of chars) {
      const key = map[ch];
      if (ch.length !== 1 || !Number.isInteger(key) || key < 0 || key > KEY_INDEX_MAX) fail('canonical.' + name);
      out.set(ch, key);
    }
    return out;
  }

  function compilePolicy(policy) {
    if (!isObj(policy) || own(policy, 'schema') !== 1) fail('schema');
    const minKeys = own(policy, 'minKeys');
    const maxKeys = own(policy, 'maxKeys');
    if (!Number.isInteger(minKeys) || !Number.isInteger(maxKeys) || minKeys < 1 || minKeys > maxKeys ||
        maxKeys > MAX_KEYS_LIMIT) fail('key bounds');
    if (!sameList(own(policy, 'profiles'), PROFILES)) fail('profiles');
    if (!sameList(own(policy, 'models'), MODELS)) fail('models');
    const canonical = own(policy, 'canonical');
    if (!isObj(canonical)) fail('canonical');
    const maps = { us: compileCanonical(own(canonical, 'us'), 'us'), ru: compileCanonical(own(canonical, 'ru'), 'ru') };
    const rawOutcomes = own(policy, 'outcomes');
    if (!Array.isArray(rawOutcomes) || rawOutcomes.length === 0 || rawOutcomes.length > MAX_OUTCOMES) fail('outcomes');
    const outcomes = [];
    for (let i = 0; i < rawOutcomes.length; i++) {
      const o = rawOutcomes[i];
      if (!isObj(o)) fail('outcome');
      const action = own(o, 'action');
      const reason = own(o, 'reason');
      if (!ACTIONS.includes(action) || !REASONS.includes(reason)) fail('outcome');
      outcomes.push(Object.freeze({ action, reason }));
    }
    const rawTable = own(policy, 'table');
    if (!Array.isArray(rawTable) || rawTable.length !== TABLE_SIZE) fail('table');
    const table = new Uint8Array(TABLE_SIZE);
    for (let i = 0; i < TABLE_SIZE; i++) {
      const v = rawTable[i];
      if (!Number.isInteger(v) || v < 0 || v >= outcomes.length) fail('table');
      table[i] = v;
    }
    return Object.freeze({ minKeys, maxKeys, maps, outcomes: Object.freeze(outcomes), table });
  }

  // Returns { bits, model } or null on any shape violation. Exact booleans only.
  function readEvidence(raw) {
    try {
      if (!isObj(raw)) return null;
      if (typeof raw.then === 'function') return null; // Promise/thenable: provider must be synchronous
      let bits = 0;
      for (let i = 0; i < PAIRS.length; i++) {
        const pair = own(raw, PAIRS[i]);
        if (!isObj(pair)) return null;
        const observed = own(pair, 'observed');
        const alternative = own(pair, 'alternative');
        if (typeof observed !== 'boolean' || typeof alternative !== 'boolean') return null;
        if (observed) bits |= 1 << (2 * i);
        if (alternative) bits |= 2 << (2 * i);
      }
      let model = MODELS.indexOf(own(raw, 'model'));
      if (model < 0) model = INVALID_MODEL;
      return { bits, model };
    } catch (_) {
      return null;
    }
  }

  function createRouter(options) {
    if (!isObj(options)) fail('options');
    const policy = compilePolicy(own(options, 'policy'));
    const convert = own(options, 'convert');
    const evidence = own(options, 'evidence');
    if (typeof convert !== 'function') fail('convert');
    if (evidence !== undefined && evidence !== null && typeof evidence !== 'function') fail('evidence');

    const result = (action, reason, evidenceQueried, candidate) => ({ action, reason, evidenceQueried, candidate });
    const abstain = (reason, queried) => result('abstain', reason, queried, '');

    function route(request) {
      let layout;
      let profileIndex;
      let token;
      try {
        if (!isObj(request)) return abstain('invalidRequest', false);
        const field = request.field;
        if (field === 'protected') return result('keep', 'protectedField', false, '');
        if (field !== 'prose') return abstain('unknownContext', false);
        layout = request.layout;
        if (layout !== 'us' && layout !== 'ru') return abstain('unknownContext', false);
        profileIndex = PROFILES.indexOf(request.profile);
        if (profileIndex < 0) return abstain('invalidRequest', false);
        token = request.token;
      } catch (_) {
        return abstain('invalidRequest', false);
      }
      if (typeof token !== 'string') return abstain('invalidRequest', false);
      if (token.length > policy.maxKeys) return abstain('tooLong', false);
      if (token.length === 0) return abstain('unsupportedToken', false);
      const map = policy.maps[layout];
      const keys = [];
      for (let i = 0; i < token.length; i++) {
        const key = map.get(token[i]);
        if (key === undefined) return abstain('unsupportedToken', false);
        keys.push(key);
      }
      if (token.length < policy.minKeys) return abstain('tooShort', false);
      if (!evidence) return abstain('evidenceUnavailable', false);

      let raw;
      try {
        raw = apply(evidence, undefined, [Object.freeze({ token, layout, keys: Object.freeze(keys) })]);
      } catch (_) {
        return abstain('evidenceUnavailable', true);
      }
      if (raw === null) return abstain('evidenceUnavailable', true);
      const parsed = readEvidence(raw);
      if (parsed === null) return abstain('invalidEvidence', true);

      const index = (profileIndex * MODELS.length + parsed.model) * 64 + parsed.bits;
      const outcome = policy.outcomes[policy.table[index]];
      if (outcome.action !== 'correct' && outcome.action !== 'suggest') {
        return result(outcome.action, outcome.reason, true, '');
      }
      let candidate;
      try {
        candidate = apply(convert, undefined, [token, DIRECTION[layout]]);
      } catch (_) {
        return abstain('invalidEvidence', true);
      }
      if (typeof candidate !== 'string' || candidate.length !== token.length) return abstain('invalidEvidence', true);
      const opposite = policy.maps[layout === 'us' ? 'ru' : 'us'];
      for (let i = 0; i < candidate.length; i++) {
        if (opposite.get(candidate[i]) !== keys[i]) return abstain('invalidEvidence', true);
      }
      return result(outcome.action, outcome.reason, true, candidate);
    }

    // Only Correct may change text automatically; Suggest never does.
    function decideUs(token) {
      const r = route({ field: 'prose', layout: 'us', token, profile: 'safeConfirmed' });
      return r.action === 'correct' ? r.candidate : null;
    }

    return Object.freeze({ route, decideUs });
  }

  return Object.freeze({ createRouter });
});
