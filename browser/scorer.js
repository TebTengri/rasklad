/* Project license pending; see README.md
 * Browser arithmetic port of the native RSKN scorer, checked by native gold.
 * Format validation does not authenticate model provenance or release rights.
 */
'use strict';
(function (root) {
  var NATIVE = Object.freeze({
    magic: 'RSKN', version: 1, scale: 8, alphabet: 35, letterKeys: 33, bos: 33, eos: 34,
    tableSize: 44135, headerBytes: 20, serializedBytes: 88290, minQuantized: -127,
    minStepRaw: -381, minTokenKeys: 1, maxTokenKeys: 32, minDecisionKeys: 4
  });
  var BI = 35, TRI = 35 + 1225;
  var MSG = 'invalid model';
  var TA = Object.getPrototypeOf(Uint8Array.prototype);
  function getter(name) { return Object.getOwnPropertyDescriptor(TA, name).get; }
  var taBuffer = getter('buffer'), taByteLength = getter('byteLength'), taLength = getter('length');
  var taSet = TA.set, taFill = TA.fill;
  var abByteLength = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'byteLength').get;
  var abDetached = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'detached');

  function fail() { throw new TypeError(MSG); }
  function isObj(v) { return v !== null && typeof v === 'object'; }
  function invalidScore() { return Object.freeze({ ok: false, keyCount: 0, steps: 0, raw: Object.freeze([0, 0]) }); }

  function checkSpec(spec) {
    if (!isObj(spec)) fail();
    var k;
    for (k in NATIVE) { if (spec[k] !== NATIVE[k]) fail(); }
    var w = spec.orderWeights;
    if (!Array.isArray(w) || w.length !== 3) fail();
    var w0 = w[0], w1 = w[1], w2 = w[2];
    if (w0 !== 1 || w1 !== 1 || w2 !== 1) fail();
    var ts = spec.twoScore;
    if (!isObj(ts)) fail();
    var enabled = ts.enabled, minAlt = ts.minAlternative, maxObs = ts.maxObserved, gap = ts.gap;
    // Match the admitted native LocalEvidenceProvider profile, not arbitrary tuning.
    if (enabled !== true || minAlt !== -13 || maxObs !== -9 || gap !== 2.5) fail();
    return Object.freeze({
      enabled: true,
      minAlternative: typeof minAlt === 'number' ? minAlt : NaN,
      maxObserved: typeof maxObs === 'number' ? maxObs : NaN,
      gap: typeof gap === 'number' ? gap : NaN
    });
  }

  function readU32(b, o) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }

  function loadModelUnchecked(bytes, spec) {
    var two = checkSpec(spec);
    if (!(bytes instanceof Uint8Array)) fail();
    var buf = taBuffer.call(bytes);
    // The intrinsic rejects SharedArrayBuffer even if Symbol.toStringTag is spoofed.
    abByteLength.call(buf);
    if (abDetached && abDetached.get.call(buf) === true) fail();
    if (taByteLength.call(bytes) !== NATIVE.serializedBytes) fail();

    var copy = new Uint8Array(NATIVE.serializedBytes);
    taSet.call(copy, bytes);
    var ok = copy[0] === 0x52 && copy[1] === 0x53 && copy[2] === 0x4B && copy[3] === 0x4E &&
      readU32(copy, 4) === NATIVE.version && readU32(copy, 8) === NATIVE.scale &&
      readU32(copy, 12) === NATIVE.alphabet && readU32(copy, 16) === NATIVE.tableSize;
    var t = new Int8Array(copy.buffer, NATIVE.headerBytes, 2 * NATIVE.tableSize);
    for (var i = 0; ok && i < t.length; i++) { if (t[i] > 0 || t[i] < NATIVE.minQuantized) ok = false; }
    if (!ok) { taFill.call(copy, 0); fail(); }

    var disposed = false;

    function score(keys) {
      if (disposed) return invalidScore();
      var local = null;
      try {
        var n;
        if (keys instanceof Uint8Array) n = taLength.call(keys);
        else if (Array.isArray(keys)) n = keys.length;
        else return invalidScore();
        if (typeof n !== 'number' || !Number.isInteger(n) || n < NATIVE.minTokenKeys || n > NATIVE.maxTokenKeys) return invalidScore();
        local = new Uint8Array(n);
        for (var j = 0; j < n; j++) {
          var v = keys[j];
          if (typeof v !== 'number' || !Number.isInteger(v) || v < 0 || v >= NATIVE.letterKeys) { taFill.call(local, 0); return invalidScore(); }
          local[j] = v;
        }
        if (disposed) { taFill.call(local, 0); return invalidScore(); }
        var raw = [0, 0];
        for (var lang = 0; lang < 2; lang++) {
          var base = lang * NATIVE.tableSize, a = NATIVE.bos, b = NATIVE.bos, sum = 0;
          for (var s = 0; s <= n; s++) {
            var x = s < n ? local[s] : NATIVE.eos;
            sum += t[base + x] + t[base + BI + b * 35 + x] + t[base + TRI + (a * 35 + b) * 35 + x];
            a = b; b = x;
          }
          raw[lang] = sum;
        }
        taFill.call(local, 0);
        return Object.freeze({ ok: true, keyCount: n, steps: n + 1, raw: Object.freeze(raw) });
      } catch (e) {
        if (local) taFill.call(local, 0);
        return invalidScore();
      }
    }

    function decide(sc, layout) {
      try {
        if (disposed || !isObj(sc)) return 'abstain';
        var obsI, altI;
        if (layout === 'us') { obsI = 0; altI = 1; }
        else if (layout === 'ru') { obsI = 1; altI = 0; }
        else return 'abstain';
        var okv = sc.ok, count = sc.keyCount, steps = sc.steps, raw = sc.raw;
        if (okv !== true) return 'abstain';
        if (typeof count !== 'number' || !Number.isInteger(count) || count < NATIVE.minTokenKeys || count > NATIVE.maxTokenKeys) return 'abstain';
        if (steps !== count + 1) return 'abstain';
        if (!Array.isArray(raw) || raw.length !== 2) return 'abstain';
        var r = [raw[0], raw[1]], floor = steps * NATIVE.minStepRaw;
        for (var q = 0; q < 2; q++) {
          if (typeof r[q] !== 'number' || !Number.isInteger(r[q]) || r[q] < floor || r[q] > 0) return 'abstain';
        }
        if (count < NATIVE.minDecisionKeys) return 'abstain';
        if (!two.enabled || !Number.isFinite(two.minAlternative) || !Number.isFinite(two.maxObserved) ||
            !Number.isFinite(two.gap) || two.gap < 0) return 'abstain';
        var denom = NATIVE.scale * steps;
        var obs = r[obsI] / denom, alt = r[altI] / denom;
        if (alt > obs && alt >= two.minAlternative && obs <= two.maxObserved && alt - obs >= two.gap) return 'correct';
        if (obs > two.maxObserved && obs >= alt) return 'keep';
        return 'abstain';
      } catch (e) {
        return 'abstain';
      }
    }

    function dispose() {
      if (disposed) return;
      disposed = true;
      try { taFill.call(copy, 0); } catch (e) { /* best effort */ }
    }

    return Object.freeze({ score: score, decide: decide, dispose: dispose });
  }

  function loadModel(bytes, spec) {
    try { return loadModelUnchecked(bytes, spec); } catch (_) { fail(); }
  }
  var api = Object.freeze({ loadModel: loadModel });
  if (typeof module === 'object' && module && typeof module.exports === 'object') module.exports = api;
  if (root && typeof root === 'object') {
    try { Object.defineProperty(root, 'RaskladScorer', { value: api, writable: false, configurable: false, enumerable: false }); } catch (e) { /* already defined */ }
  }
})(typeof globalThis !== 'undefined' ? globalThis : (typeof self !== 'undefined' ? self : this));
