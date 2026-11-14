'use strict';
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Rasklad contributors
//
// Lifecycle glue: verified local bundle -> scorer/lexicon/spell/evidence/router/adapter.
// Trust model: the manifest is a caller-owned release catalog record, and digest/instantiate/dependencies
// are trusted explicit functions in the same JS realm. This module does no I/O, fetch, storage, timers or logging.
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module && module.exports) module.exports = api;
  else root.RaskladSession = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  const apply = Reflect.apply;
  const U8 = Uint8Array;
  const TA = Object.getPrototypeOf(U8.prototype);
  const getter = (proto, key) => Object.getOwnPropertyDescriptor(proto, key).get;
  const taTag = getter(TA, Symbol.toStringTag);
  const taBuffer = getter(TA, 'buffer');
  const taByteLength = getter(TA, 'byteLength');
  const taSet = TA.set;
  const taFill = TA.fill;
  const abByteLength = getter(ArrayBuffer.prototype, 'byteLength');
  const abDetachedDesc = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'detached');
  const abDetached = abDetachedDesc ? abDetachedDesc.get : undefined;
  const hasOwn = Object.prototype.hasOwnProperty;

  const KiB = 1024;
  const MiB = 1024 * KiB;
  const MODEL_BYTES = 88290;
  const CAPS = new Map([
    ['engine.js', 256 * KiB], ['engine.wasm', 2 * MiB], ['model.rskn', MODEL_BYTES], ['stems.rskp', 16 * MiB],
    ['packs/en/dictionary.aff', MiB], ['packs/en/dictionary.dic', 16 * MiB],
    ['packs/ru/dictionary.aff', MiB], ['packs/ru/dictionary.dic', 16 * MiB],
  ]);
  const NOTICES = [
    'packs/en/notices/UPSTREAM.txt', 'packs/en/notices/MODIFICATIONS.txt',
    'packs/ru/notices/UPSTREAM.txt', 'packs/ru/notices/MODIFICATIONS.txt',
    'notices/MODEL-MODIFICATIONS.txt', 'notices/MODEL-METADATA.json',
  ];
  const EXTRA = /^(?:(?:packs\/(?:en|ru)\/)?notices\/.+|packs\/(?:en|ru)\/manifest\.json)$/;
  const NAME = /^[A-Za-z0-9._-]+(?:\/[A-Za-z0-9._-]+)*$/;
  const HEX = /^[0-9a-f]{64}$/;
  const ID = /^[a-z0-9-]{1,64}$/;
  const LOCALES = { en: 'en-US', ru: 'ru-RU' };
  // Adapter/router decideUs contract is candidate string or null.
  const UNAVAILABLE = null;

  function reject() {
    throw new TypeError('unavailable local profile');
  }

  const isObj = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
  const num = (v) => (Number.isSafeInteger(v) && v >= 0 ? v : 0);

  function exact(o, keys) {
    if (!isObj(o)) reject();
    if (Object.keys(o).length !== keys.length || !keys.every((k) => apply(hasOwn, o, [k]))) reject();
    return o;
  }

  function method(o, key) {
    const f = o !== null && (typeof o === 'object' || typeof o === 'function') ? o[key] : undefined;
    if (typeof f !== 'function') reject();
    return (...args) => apply(f, o, args);
  }

  function utf8Length(s) {
    let n = 0;
    for (let i = 0; i < s.length; i++) {
      const c = s.charCodeAt(i);
      if (c < 0x80) n += 1;
      else if (c < 0x800) n += 2;
      else if (c >= 0xd800 && c < 0xdc00 && i + 1 < s.length) { n += 4; i++; }
      else n += 3;
    }
    return n;
  }

  function deepFreeze(v) {
    if (v !== null && typeof v === 'object') {
      Object.freeze(v);
      for (const k of Object.keys(v)) deepFreeze(v[k]);
    }
    return v;
  }

  function snapshot(value, limit) {
    const s = JSON.stringify(value);
    if (typeof s !== 'string' || s.length > limit || utf8Length(s) > limit) reject();
    return deepFreeze(JSON.parse(s));
  }

  function checkFiles(files) {
    if (!Array.isArray(files) || files.length < 8 || files.length > 32) reject();
    const pins = new Map();
    const lower = new Set();
    const dirs = new Map();
    let total = 0;
    for (const entry of files) {
      const { name, bytes, sha256 } = exact(entry, ['name', 'bytes', 'sha256']);
      if (typeof name !== 'string' || name.length > 128 || !NAME.test(name)) reject();
      const segs = name.split('/');
      if (segs.length > 5 || segs.some((s) => s === '.' || s === '..')) reject();
      if (!CAPS.has(name) && !EXTRA.test(name)) reject();
      const cap = CAPS.has(name) ? CAPS.get(name) : MiB;
      if (!Number.isSafeInteger(bytes) || bytes < 1 || bytes > cap) reject();
      if (name === 'model.rskn' && bytes !== MODEL_BYTES) reject();
      if (typeof sha256 !== 'string' || !HEX.test(sha256)) reject();
      const lo = name.toLowerCase();
      if (lower.has(lo)) reject();
      lower.add(lo);
      for (let i = 1; i < segs.length; i++) {
        const directory=segs.slice(0,i).join('/'), folded=directory.toLowerCase();
        if (dirs.has(folded) && dirs.get(folded)!==directory) reject();
        dirs.set(folded,directory);
      }
      total += bytes;
      pins.set(name, Object.freeze({ bytes, sha256 }));
    }
    for (const lo of lower) if (dirs.has(lo)) reject();
    if (total > 32 * MiB) reject();
    for (const required of [...CAPS.keys(), ...NOTICES]) if (!pins.has(required)) reject();
    return pins;
  }

  function checkManifest(m, purpose) {
    exact(m, ['schema', 'id', 'admission', 'engine', 'model', 'stems', 'languages', 'files']);
    const adm = exact(m.admission, ['local_test', 'distribution']);
    if (m.schema !== 1 || typeof m.id !== 'string' || !ID.test(m.id)) reject();
    if (typeof adm.local_test !== 'boolean' || typeof adm.distribution !== 'boolean') reject();
    if (purpose === 'local-test' ? !adm.local_test : purpose !== 'distribution' || !adm.distribution) reject();
    const eng = exact(m.engine, ['version', 'loader', 'wasm']);
    if (eng.version !== '1.7.5' || eng.loader !== 'engine.js' || eng.wasm !== 'engine.wasm') reject();
    if (m.model !== 'model.rskn' || m.stems !== 'stems.rskp') reject();
    exact(m.languages, ['en', 'ru']);
    for (const lang of ['en', 'ru']) {
      const l = exact(m.languages[lang], ['locale', 'aff', 'dic']);
      if (l.locale !== LOCALES[lang]) reject();
      if (l.aff !== `packs/${lang}/dictionary.aff` || l.dic !== `packs/${lang}/dictionary.dic`) reject();
    }
    return checkFiles(m.files);
  }

  function ownCopy(v, n) {
    if (apply(taTag, v, []) !== 'Uint8Array') reject();
    const buf = apply(taBuffer, v, []);
    apply(abByteLength, buf, []); // throws for SharedArrayBuffer
    if (abDetached && apply(abDetached, buf, [])) reject();
    if (apply(taByteLength, v, []) !== n) reject();
    const copy = new U8(n);
    apply(taSet, copy, [v]);
    return copy;
  }

  function splitStems(b) {
    const n = apply(taByteLength, b, []);
    const magic = (p, last) => p[0] === 0x52 && p[1] === 0x53 && p[2] === 0x4b && p[3] === last;
    if (n < 20 || !magic(b, 0x50)) reject();
    const u32 = (o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
    const en = u32(12);
    const ru = u32(16);
    if (u32(4) !== 1 || u32(8) !== 2 || en < 1 || ru < 1 || 20 + en + ru !== n) reject();
    const buf = apply(taBuffer, b, []);
    const parts = [new U8(buf, 20, en), new U8(buf, 20 + en, ru)];
    for (const p of parts) if (!magic(p, 0x46)) reject();
    return parts;
  }

  function wipe(buffers) {
    for (const b of buffers) {
      try { apply(taFill, b, [0]); } catch { /* best effort */ }
    }
  }

  async function createSession(options) {
    const owned = [];
    const stack = [];
    let state = 'loading';
    let engStats, routerDecide, adapterRegister, adapterUnregister;
    let modelBytes = 0;
    let stemBytes = 0;

    const cleanup = () => {
      while (stack.length) {
        const dispose = stack.pop();
        try { dispose(); } catch { /* continue cleanup */ }
      }
      engStats = routerDecide = adapterRegister = adapterUnregister = undefined;
    };
    const probe = () => {
      try {
        const s = engStats();
        return isObj(s) && s.status === 'ready' && s.engineCount === 4 &&
          Number.isSafeInteger(s.heapBytes) && s.heapBytes > 0 && s.heapBytes <= 128 * MiB ? s : null;
      } catch {
        return null;
      }
    };
    const failSession = () => {
      if (state !== 'ready') return;
      state = 'failed';
      cleanup();
    };
    const live = () => {
      if (state === 'ready' && !probe()) failSession();
      return state === 'ready';
    };
    const guard = (fn) => (...args) => {
      if (state !== 'ready' && state !== 'loading') reject();
      return fn(...args);
    };

    function decideUs(token) {
      if (!live()) return UNAVAILABLE;
      try {
        const result = routerDecide(token);
        if (live()) return result;
      } catch {
        failSession();
      }
      return UNAVAILABLE;
    }

    function register(element, opts) {
      if (!live()) reject();
      return adapterRegister(element, opts);
    }

    function unregister(element) {
      if (state === 'ready') return adapterUnregister(element);
      return undefined;
    }

    function dispose() {
      if (state !== 'ready') return;
      state = 'disposed';
      cleanup();
    }

    function stats() {
      const s = state === 'ready' ? probe() : null;
      if (state === 'ready' && !s) failSession();
      const ok = state === 'ready';
      return Object.freeze({
        status: state,
        heapBytes: ok ? num(s.heapBytes) : 0,
        engineCount: ok ? num(s.engineCount) : 0,
        modelBytes: ok ? modelBytes : 0,
        stemBytes: ok ? stemBytes : 0,
      });
    }

    try {
      // Everything up to the first await: snapshot manifest/policy, copy assets, bind dependencies.
      if (!isObj(options)) reject();
      const { manifest, assets, purpose = 'local-test', digest, instantiate, dependencies, policy, convert, document } = options;
      const m = snapshot(manifest, 64 * KiB);
      const pol = snapshot(policy, 128 * KiB);
      if (!isObj(pol) || typeof digest !== 'function' || typeof instantiate !== 'function') reject();
      if (typeof convert !== 'function' || !isObj(document)) reject();
      const pins = checkManifest(m, purpose);

      const proto = isObj(assets) ? Object.getPrototypeOf(assets) : undefined;
      if (proto !== Object.prototype && proto !== null) reject();
      const keys = Reflect.ownKeys(assets);
      if (keys.length !== pins.size) reject();
      const copies = new Map();
      for (const k of keys) {
        const d = typeof k === 'string' && pins.has(k) && !copies.has(k) ? Object.getOwnPropertyDescriptor(assets, k) : undefined;
        if (!d || !('value' in d)) reject();
        const copy = ownCopy(d.value, pins.get(k).bytes);
        owned.push(copy);
        copies.set(k, copy);
      }
      if (copies.size !== pins.size) reject();

      exact(dependencies, ['scorer', 'lexicon', 'spell', 'evidence', 'router', 'adapter']);
      const loadModel = method(dependencies.scorer, 'loadModel');
      const loadLexicon = method(dependencies.lexicon, 'loadLexicon');
      const createEngines = method(dependencies.spell, 'createEngines');
      const createEvidence = method(dependencies.evidence, 'createEvidenceProvider');
      const createRouter = method(dependencies.router, 'createRouter');
      const createAdapter = method(dependencies.adapter, 'createAdapter');

      for (const [name, copy] of copies) {
        const pin = pins.get(name);
        const hash = await digest(copy);
        if (typeof hash !== 'string' || hash !== pin.sha256 || apply(taByteLength, copy, []) !== pin.bytes) reject();
      }

      // Data formats are validated before any engine code executes.
      const get = (name) => copies.get(name);
      const model = loadModel(get('model.rskn'), pol.modelSpec);
      stack.push(method(model, 'dispose'));
      const [enStemBytes, ruStemBytes] = splitStems(get('stems.rskp'));
      const stemsEn = loadLexicon(enStemBytes);
      stack.push(method(stemsEn, 'dispose'));
      const stemsRu = loadLexicon(ruStemBytes);
      stack.push(method(stemsRu, 'dispose'));
      const enHas = method(stemsEn, 'contains');
      const ruHas = method(stemsRu, 'contains');

      const wasmModule = await instantiate({ loader: get('engine.js'), wasm: get('engine.wasm') });
      const engines = createEngines(wasmModule, {
        en: { aff: get('packs/en/dictionary.aff'), dic: get('packs/en/dictionary.dic') },
        ru: { aff: get('packs/ru/dictionary.aff'), dic: get('packs/ru/dictionary.dic') },
      });
      stack.push(method(engines, 'dispose'));
      engStats = method(engines, 'stats');
      if (!probe()) reject();

      // full/control are the engine set's full-dictionary and control-dictionary lookups.
      const lookup = Object.freeze({
        full: guard(method(engines, 'full')),
        control: guard(method(engines, 'control')),
        stem: guard((lang, stemKeys) => {
          if (lang === 'en') return enHas(stemKeys);
          if (lang === 'ru') return ruHas(stemKeys);
          return reject();
        }),
      });
      const evidence = createEvidence({ model, policy: pol, convert, lookup });
      const router = createRouter({ policy: pol, convert, evidence });
      routerDecide = method(router, 'decideUs');
      const adapter = createAdapter({ document, decide: (token) => decideUs(token) });
      stack.push(method(adapter, 'dispose'));
      adapterRegister = method(adapter, 'register');
      adapterUnregister = method(adapter, 'unregister');

      modelBytes = pins.get('model.rskn').bytes;
      stemBytes = pins.get('stems.rskp').bytes;
      state = 'ready';
    } catch {
      state = 'failed';
      cleanup();
      throw new TypeError('unavailable local profile');
    } finally {
      wipe(owned);
    }

    return Object.freeze({ register, unregister, decideUs, dispose, stats });
  }

  return Object.freeze({ createSession });
});
