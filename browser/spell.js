// SPDX-License-Identifier: MIT
//
// browser/spell.js - RaskladSpell, a thin synchronous adapter over the C API of an
// externally supplied, pre-initialized Emscripten Hunspell-compatible module (SYS-242).
//
// Scope and status notes:
//  - This is an original adapter. It does not reimplement Hunspell. It does not load,
//    fetch, or initialize code. The caller supplies an already-awaited module instance.
//  - Core version 1.7.5 is required; integrity is checked by the caller. Nothing
//    here implies the module is release-ready or replaces the project's data-admission
//    process for engines or dictionaries.
//  - The module boundary is trusted. This code is not a security boundary against
//    malicious code running in the same realm.
//  - Lookups only. There are no suggestions, no dictionary mutation, no logging, no
//    history, and no retention of queried tokens.
(function (root) {
  'use strict';

  var INVALID_PROFILE = 'invalid spell profile';
  var INVALID_QUERY = 'invalid spell query';
  var UNAVAILABLE = 'spell unavailable';

  var MAX_AFF_BYTES = 1024 * 1024; // Matches native profile::kMaxAffBytes.
  var MAX_DIC_BYTES = 16 * 1024 * 1024;
  var MAX_TOKEN_UNITS = 32;
  var MAX_ENCODING_BYTES = 32;
  var LANGS = ['en', 'ru'];

  // Captured intrinsics.
  var apply = Reflect.apply;
  var freeze = Object.freeze;
  var isSafeInteger = Number.isSafeInteger;
  var U8 = Uint8Array;
  var TypedArrayProto = Object.getPrototypeOf(Uint8Array.prototype);

  function getterOf(obj, key) {
    var d = Object.getOwnPropertyDescriptor(obj, key);
    return d && typeof d.get === 'function' ? d.get : null;
  }

  var taTagGet = getterOf(TypedArrayProto, Symbol.toStringTag);
  var taBufferGet = getterOf(TypedArrayProto, 'buffer');
  var taLengthGet = getterOf(TypedArrayProto, 'length');
  var taByteLengthGet = getterOf(TypedArrayProto, 'byteLength');
  var abByteLengthGet = getterOf(ArrayBuffer.prototype, 'byteLength');
  var abDetachedGet = getterOf(ArrayBuffer.prototype, 'detached');
  var taSet = TypedArrayProto.set;
  var taFill = TypedArrayProto.fill;

  var decoder = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true });
  var decode = TextDecoder.prototype.decode;
  var encoder = new TextEncoder();
  var encode = TextEncoder.prototype.encode;

  var factorySeq = 0;

  function isU8(x) {
    if (x === null || (typeof x !== 'object' && typeof x !== 'function')) return false;
    try {
      return apply(taTagGet, x, []) === 'Uint8Array';
    } catch (e) {
      return false;
    }
  }

  function u8Length(x) {
    return apply(taLengthGet, x, []);
  }

  // Returns an owned copy of a genuine, non-shared, non-detached, non-empty Uint8Array
  // no longer than cap. Returns null otherwise.
  function ownedSnapshot(x, cap) {
    if (!isU8(x)) return null;
    var len;
    try {
      var buf = apply(taBufferGet, x, []);
      apply(abByteLengthGet, buf, []); // throws for SharedArrayBuffer
      if (abDetachedGet && apply(abDetachedGet, buf, []) === true) return null;
      len = u8Length(x);
    } catch (e) {
      return null;
    }
    if (!isSafeInteger(len) || len < 1 || len > cap) return null;
    var copy = new U8(len);
    apply(taSet, copy, [x]);
    if (u8Length(copy) !== len) return null;
    return copy;
  }

  function hasNul(bytes) {
    for (var i = 0; i < bytes.length; i++) {
      if (bytes[i] === 0) return true;
    }
    return false;
  }

  function isStrictUtf8(bytes) {
    try {
      apply(decode, decoder, [bytes]);
      return true;
    } catch (e) {
      return false;
    }
  }

  function wipe(bytes) {
    if (!bytes) return;
    try {
      apply(taFill, bytes, [0]);
    } catch (e) {
      // ignore
    }
  }

  function isSpaceOrTab(c) {
    return c === 0x20 || c === 0x09;
  }

  // Port of removePfxSfxRows. src must be an owned Uint8Array. Returns a new Uint8Array
  // or null when the input is rejected.
  function removePfxSfxRowsCore(src) {
    var n = src.length;
    if (n > MAX_AFF_BYTES || n === 0 || hasNul(src)) return null;
    // Bounded byte workspace; do not allocate per-row JS metadata for huge aff files.
    var workspace = new U8(n);
    var total = 0;
    var removed = 0;
    var start = 0;
    while (start < n) {
      var newline = -1;
      for (var i = start; i < n; i++) {
        if (src[i] === 0x0a) {
          newline = i;
          break;
        }
      }
      var end = newline < 0 ? n : newline + 1;
      var b = start;
      var e = end;
      if (e > start && src[e - 1] === 0x0a) --e;
      if (e > start && src[e - 1] === 0x0d) --e;
      var bomRow = false;
      if (start === 0 && end - start >= 3 &&
          src[0] === 0xef && src[1] === 0xbb && src[2] === 0xbf) {
        b = 3;
        bomRow = true;
      }
      while (b < e && isSpaceOrTab(src[b])) ++b;
      var f = b;
      while (f < e && !isSpaceOrTab(src[f])) ++f;
      var fieldLen = f - b;
      // "PFX" = 50 46 58, "SFX" = 53 46 58
      var startsAffix = fieldLen >= 3 &&
        (src[b] === 0x50 || src[b] === 0x53) &&
        src[b + 1] === 0x46 && src[b + 2] === 0x58;
      var exact = startsAffix && fieldLen === 3;
      if (!exact && startsAffix) return null;
      if (exact && bomRow) return null;
      if (exact) {
        ++removed;
      } else {
        for (var kept = start; kept < end; kept++) workspace[total++] = src[kept];
      }
      start = end;
    }
    if (removed === 0) return null;
    var out = new U8(total);
    for (var k = 0; k < total; k++) out[k] = workspace[k];
    wipe(workspace);
    return out;
  }

  function removeAffixRows(bytes) {
    var snap = ownedSnapshot(bytes, MAX_AFF_BYTES);
    if (!snap) throw new TypeError(INVALID_PROFILE);
    var out = removePfxSfxRowsCore(snap);
    wipe(snap);
    if (!out) throw new TypeError(INVALID_PROFILE);
    return out;
  }

  // Port of isQueryableToken over UTF-16 code units.
  function isQueryableToken(token) {
    if (typeof token !== 'string') return false;
    var n = token.length;
    if (n < 1 || n > MAX_TOKEN_UNITS) return false;
    for (var i = 0; i < n; i++) {
      var c = token.charCodeAt(i);
      if (c <= 0x1f || (c >= 0x7f && c <= 0x9f)) return false; // Other_Control (Cc)
      if (c >= 0xd800 && c <= 0xdbff) {
        if (i + 1 >= n) return false;
        var d = token.charCodeAt(i + 1);
        if (d < 0xdc00 || d > 0xdfff) return false;
        ++i;
      } else if (c >= 0xdc00 && c <= 0xdfff) {
        return false;
      }
    }
    return true;
  }

  function bindModule(mod) {
    if (mod === null || (typeof mod !== 'object' && typeof mod !== 'function')) return null;
    try {
      if (typeof mod.then === 'function') return null; // unresolved factory promise
      var fs = mod.FS;
      if (fs === null || (typeof fs !== 'object' && typeof fs !== 'function')) return null;
      var b = {
        mod: mod,
        fs: fs,
        mkdir: fs.mkdir,
        writeFile: fs.writeFile,
        unlink: fs.unlink,
        rmdir: fs.rmdir,
        malloc: mod._malloc,
        free: mod._free,
        create: mod._Hunspell_create,
        destroy: mod._Hunspell_destroy,
        spell: mod._Hunspell_spell,
        encoding: mod._Hunspell_get_dic_encoding,
        version: mod._Hunspell_get_library_version
      };
      var names = ['mkdir', 'writeFile', 'unlink', 'rmdir', 'malloc', 'free',
        'create', 'destroy', 'spell', 'encoding', 'version'];
      for (var i = 0; i < names.length; i++) {
        if (typeof b[names[i]] !== 'function') return null;
      }
      if (!isU8(mod.HEAPU8)) return null;
      var vp = apply(b.version, mod, []);
      var expected = [49, 46, 55, 46, 53, 0];
      if (!Number.isInteger(vp) || vp <= 0 || vp + expected.length > mod.HEAPU8.length) return null;
      for (var vi = 0; vi < expected.length; vi++) if (mod.HEAPU8[vp + vi] !== expected[vi]) return null;
      return b;
    } catch (e) {
      return null;
    }
  }

  function currentHeap(m) {
    var heap = m.mod.HEAPU8;
    if (!isU8(heap)) throw new Error(UNAVAILABLE);
    return heap;
  }

  function toCInt(r) {
    if (typeof r !== 'number' || !isSafeInteger(r) || r < -2147483648 || r > 2147483647) {
      throw new Error(UNAVAILABLE);
    }
    return r;
  }

  function isPointerIn(heap, ptr, size) {
    return typeof ptr === 'number' && isSafeInteger(ptr) && ptr > 0 &&
      ptr + size <= u8Length(heap);
  }

  // Copies bytes plus a NUL terminator into fresh WASM memory and returns the pointer.
  function allocCString(m, bytes) {
    var size = bytes.length + 1;
    var ptr = apply(m.malloc, m.mod, [size]);
    if (typeof ptr !== 'number' || !isSafeInteger(ptr) || ptr <= 0) {
      throw new Error(UNAVAILABLE);
    }
    var heap = currentHeap(m); // re-read after allocation; memory may have grown
    if (!isPointerIn(heap, ptr, size)) {
      try {
        apply(m.free, m.mod, [ptr]);
      } catch (e) {
        // ignore
      }
      throw new Error(UNAVAILABLE);
    }
    apply(taSet, heap, [bytes, ptr]);
    heap[ptr + bytes.length] = 0;
    return ptr;
  }

  // Wipes the C string in the current heap, then frees it. Returns false on failure.
  function releaseCString(m, ptr, size) {
    var ok = true;
    try {
      var heap = currentHeap(m);
      if (isPointerIn(heap, ptr, size)) {
        apply(taFill, heap, [0, ptr, ptr + size]);
      } else {
        ok = false;
      }
    } catch (e) {
      ok = false;
    }
    try {
      apply(m.free, m.mod, [ptr]);
    } catch (e) {
      ok = false;
    }
    return ok;
  }

  function encodeAscii(path) {
    return apply(encode, encoder, [path]);
  }

  function readEncodingIsUtf8(m, handle) {
    var p = apply(m.encoding, m.mod, [handle]);
    var heap = currentHeap(m);
    if (typeof p !== 'number' || !isSafeInteger(p) || p <= 0) return false;
    var len = u8Length(heap);
    var limit = Math.min(len, p + MAX_ENCODING_BYTES + 1);
    var nul = -1;
    for (var i = p; i < limit; i++) {
      if (heap[i] === 0) {
        nul = i;
        break;
      }
    }
    if (nul < 0) return false;
    var expected = [0x55, 0x54, 0x46, 0x2d, 0x38]; // "UTF-8"
    if (nul - p !== expected.length) return false;
    for (var k = 0; k < expected.length; k++) {
      if (heap[p + k] !== expected[k]) return false;
    }
    return true;
  }

  function createHandle(m, handles, lang, kind, affPath, dicPath) {
    var affBytes = encodeAscii(affPath);
    var dicBytes = encodeAscii(dicPath);
    var affPtr = 0;
    var dicPtr = 0;
    var freedOk = true;
    var h;
    try {
      affPtr = allocCString(m, affBytes);
      dicPtr = allocCString(m, dicBytes);
      h = apply(m.create, m.mod, [affPtr, dicPtr]);
    } finally {
      if (dicPtr && !releaseCString(m, dicPtr, dicBytes.length + 1)) freedOk = false;
      if (affPtr && !releaseCString(m, affPtr, affBytes.length + 1)) freedOk = false;
    }
    var heap = currentHeap(m);
    if (!isPointerIn(heap, h, 1)) throw new Error(UNAVAILABLE);
    handles[lang][kind] = h;
    if (!freedOk) throw new Error(UNAVAILABLE);
    if (!readEncodingIsUtf8(m, h)) throw new Error(UNAVAILABLE);
  }

  function destroyAll(m, handles) {
    for (var i = 0; i < LANGS.length; i++) {
      var slot = handles[LANGS[i]];
      var kinds = ['full', 'control'];
      for (var k = 0; k < kinds.length; k++) {
        var h = slot[kinds[k]];
        if (h) {
          slot[kinds[k]] = 0;
          try {
            apply(m.destroy, m.mod, [h]);
          } catch (e) {
            // ignore native failures during cleanup
          }
        }
      }
    }
  }

  function countLive(handles) {
    var n = 0;
    for (var i = 0; i < LANGS.length; i++) {
      if (handles[LANGS[i]].full) n++;
      if (handles[LANGS[i]].control) n++;
    }
    return n;
  }

  function readLanguageData(entry, owned) {
    if (entry === null || typeof entry !== 'object') return null;
    var rawAff = entry.aff;
    var rawDic = entry.dic;
    var aff = ownedSnapshot(rawAff, MAX_AFF_BYTES);
    if (!aff) return null;
    owned.push(aff);
    var dic = ownedSnapshot(rawDic, MAX_DIC_BYTES);
    if (!dic) return null;
    owned.push(dic);
    if (hasNul(aff) || hasNul(dic)) return null;
    if (!isStrictUtf8(aff) || !isStrictUtf8(dic)) return null;
    var control = removePfxSfxRowsCore(aff);
    if (!control) return null;
    owned.push(control);
    if (!isStrictUtf8(control)) return null;
    return { aff: aff, control: control, dic: dic };
  }

  function readProfile(profile, owned) {
    if (profile === null || typeof profile !== 'object') return null;
    var rawEn = profile.en;
    var rawRu = profile.ru;
    var en = readLanguageData(rawEn, owned);
    if (!en) return null;
    var ru = readLanguageData(rawRu, owned);
    if (!ru) return null;
    return { en: en, ru: ru };
  }

  function buildHandles(m, data, handles) {
    var dir = '/rasklad-spell-' + (++factorySeq);
    var files = [];
    var dirMade = false;
    var built = false;
    var cleaned = true;
    function put(path, bytes) {
      files.push(path);
      apply(m.writeFile, m.fs, [path, bytes]);
    }
    try {
      apply(m.mkdir, m.fs, [dir]);
      dirMade = true;
      for (var i = 0; i < LANGS.length; i++) {
        var lang = LANGS[i];
        var d = data[lang];
        var affPath = dir + '/' + lang + '.aff';
        var controlPath = dir + '/' + lang + '-control.aff';
        var dicPath = dir + '/' + lang + '-words.dic';
        put(affPath, d.aff);
        put(controlPath, d.control);
        put(dicPath, d.dic);
        createHandle(m, handles, lang, 'full', affPath, dicPath);
        createHandle(m, handles, lang, 'control', controlPath, dicPath);
      }
      built = true;
    } finally {
      for (var f = files.length - 1; f >= 0; f--) {
        try {
          apply(m.unlink, m.fs, [files[f]]);
        } catch (e) {
          cleaned = false;
        }
      }
      if (dirMade) {
        try {
          apply(m.rmdir, m.fs, [dir]);
        } catch (e) {
          cleaned = false;
        }
      }
    }
    return built && cleaned;
  }

  function makeApi(m, handles) {
    var status = 'ready';

    function lookup(kind, language, token) {
      if (status !== 'ready') throw new Error(UNAVAILABLE);
      if (language !== 'en' && language !== 'ru') throw new TypeError(INVALID_QUERY);
      if (!isQueryableToken(token)) return false;
      var handle = handles[language][kind];
      var bytes = null;
      var ptr = 0;
      var size = 0;
      var result = false;
      var trapped = false;
      try {
        bytes = apply(encode, encoder, [token]);
        size = bytes.length + 1;
        ptr = allocCString(m, bytes);
        var r = apply(m.spell, m.mod, [handle, ptr]);
        result = toCInt(r) !== 0;
      } catch (e) {
        trapped = true;
      } finally {
        wipe(bytes);
        bytes = null;
        if (ptr && !releaseCString(m, ptr, size)) trapped = true;
        ptr = 0;
      }
      if (trapped) {
        status = 'failed';
        throw new Error(UNAVAILABLE);
      }
      return result;
    }

    function full(language, token) {
      return lookup('full', language, token);
    }

    function control(language, token) {
      return lookup('control', language, token);
    }

    function dispose() {
      if (status === 'disposed') return;
      status = 'disposed';
      destroyAll(m, handles);
    }

    function stats() {
      var heapBytes = 0;
      try {
        var heap = m.mod.HEAPU8;
        if (isU8(heap)) heapBytes = apply(taByteLengthGet, heap, []);
      } catch (e) {
        heapBytes = 0;
      }
      return freeze({
        status: status,
        engineCount: countLive(handles),
        heapBytes: heapBytes
      });
    }

    return freeze({ full: full, control: control, dispose: dispose, stats: stats });
  }

  function createEngines(module, profile) {
    var m = bindModule(module);
    if (!m) throw new TypeError(INVALID_PROFILE);
    var owned = [];
    var data = null;
    try {
      data = readProfile(profile, owned);
    } catch (e) {
      data = null;
    }
    if (!data) {
      for (var i = 0; i < owned.length; i++) wipe(owned[i]);
      throw new TypeError(INVALID_PROFILE);
    }
    var handles = {
      en: { full: 0, control: 0 },
      ru: { full: 0, control: 0 }
    };
    var ok = false;
    try {
      ok = buildHandles(m, data, handles);
    } catch (e) {
      ok = false;
    } finally {
      for (var j = 0; j < owned.length; j++) wipe(owned[j]);
      owned.length = 0;
      data = null;
    }
    if (!ok) {
      destroyAll(m, handles);
      throw new TypeError(INVALID_PROFILE);
    }
    return makeApi(m, handles);
  }

  var api = freeze({ createEngines: createEngines, removeAffixRows: removeAffixRows });

  if (typeof module === 'object' && module !== null && typeof module.exports === 'object') {
    module.exports = api;
  }
  if (root && typeof root === 'object') {
    try {
      Object.defineProperty(root, 'RaskladSpell', {
        value: api,
        writable: false,
        enumerable: false,
        configurable: false
      });
    } catch (e) {
      // already defined; leave existing binding untouched
    }
  }
})(typeof globalThis !== 'undefined' ? globalThis : (typeof self !== 'undefined' ? self : this));
