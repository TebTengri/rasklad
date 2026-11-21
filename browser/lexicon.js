/* SPDX-License-Identifier: MIT */
'use strict';
/*
 * browser/lexicon.js — RSKF v1 canonical-stem front-coded lexicon reader (SYS-242).
 *
 * Browser port of the deserialize/contains semantics of the native
 * FrontCodedLexicon. It answers exact membership for canonical key sequences
 * (bytes 0..32, length 1..32) stored in an RSKF v1 blob. It does not provide
 * Hunspell coverage or any claim of language correctness. RSKP pair files are
 * not parsed here.
 *
 * Exports: CommonJS { loadLexicon(bytes) }, or the global RaskladLexicon when
 * no CommonJS module object exists.
 *
 * Memory: the payload is kept compact. The module keeps an owned offset array
 * and a fixed-stride (33-byte) first-key directory. It does not keep strings,
 * Sets, or a decoded word list, and it does not store queries.
 */
(function (root) {
  // Intrinsics are captured at load time so that later prototype patching
  // cannot redirect the brand checks or the copy operations.
  var U8 = Uint8Array;
  var U32 = Uint32Array;
  var TypeErrorCtor = TypeError;
  var apply = Reflect.apply;
  var getProto = Object.getPrototypeOf;
  var getDesc = Object.getOwnPropertyDescriptor;
  var freeze = Object.freeze;
  var isArray = Array.isArray;
  var U8P = U8.prototype;
  var TAP = getProto(U8P);
  var taTagGet = getDesc(TAP, Symbol.toStringTag).get;
  var taBufferGet = getDesc(TAP, 'buffer').get;
  var taLengthGet = getDesc(TAP, 'length').get;
  var taSet = TAP.set;
  var taFill = TAP.fill;
  var abByteLengthGet = getDesc(ArrayBuffer.prototype, 'byteLength').get;
  var abDetachedDesc = getDesc(ArrayBuffer.prototype, 'detached');
  var abDetachedGet = abDetachedDesc && typeof abDetachedDesc.get === 'function' ? abDetachedDesc.get : null;

  var MSG = 'invalid lexicon';
  var FORMAT_VERSION = 1;
  var BLOCK_SIZE = 16;
  var HEADER_BYTES = 24;
  var MAX_KEYS = 1048576;
  var MAX_SERIALIZED = 64 * 1024 * 1024;
  var MAX_KEY_LEN = 32;
  var LETTER_KEYS = 33; // valid key byte: 0..32
  var FIRST_STRIDE = 1 + MAX_KEY_LEN;

  function wipe(arr) {
    if (arr) {
      try { apply(taFill, arr, [0]); } catch (e) { /* best effort */ }
    }
  }

  function readU32(d, o) {
    return (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)) >>> 0;
  }

  function compareBuffers(a, aLen, b, bLen) {
    var common = aLen < bLen ? aLen : bLen;
    for (var i = 0; i < common; i++) {
      if (a[i] !== b[i]) return a[i] < b[i] ? -1 : 1;
    }
    if (aLen === bLen) return 0;
    return aLen < bLen ? -1 : 1;
  }

  function commonPrefix(a, aLen, b, bLen) {
    var common = aLen < bLen ? aLen : bLen;
    var i = 0;
    while (i < common && a[i] === b[i]) i++;
    return i;
  }

  // Copies a genuine, exact, non-shared, non-detached Uint8Array into an owned
  // buffer. The size cap is checked before any allocation or copy.
  function snapshotInput(bytes) {
    if (bytes === null || typeof bytes !== 'object') return null;
    // The internal-slot brand check: a Proxy or a look-alike object yields undefined.
    if (apply(taTagGet, bytes, []) !== 'Uint8Array') return null;
    if (getProto(bytes) !== U8P) return null;
    var buf = apply(taBufferGet, bytes, []);
    apply(abByteLengthGet, buf, []); // throws for SharedArrayBuffer
    if (abDetachedGet !== null && apply(abDetachedGet, buf, []) !== false) return null;
    var len = apply(taLengthGet, bytes, []);
    if (typeof len !== 'number' || len < HEADER_BYTES || len > MAX_SERIALIZED) return null;
    var own = new U8(len);
    apply(taSet, own, [bytes, 0]);
    return own;
  }

  function parseOwned(d) {
    var len = d.length;
    if (d[0] !== 0x52 || d[1] !== 0x53 || d[2] !== 0x4b || d[3] !== 0x46) return null; // "RSKF"
    if (readU32(d, 4) !== FORMAT_VERSION || readU32(d, 12) !== BLOCK_SIZE) return null;
    var count = readU32(d, 8);
    var blockCount = readU32(d, 16);
    var payloadBytes = readU32(d, 20);
    if (count > MAX_KEYS) return null;
    if (blockCount !== Math.ceil(count / BLOCK_SIZE)) return null;
    if (HEADER_BYTES + 4 * blockCount + payloadBytes !== len) return null;
    if (payloadBytes < 3 * count) return null;
    if (count === 0 && (payloadBytes !== 0 || blockCount !== 0 || len !== HEADER_BYTES)) return null;

    var offsets = new U32(blockCount);
    var payload = null;
    var firstKeys = null;
    var prev = null;
    var cur = null;
    var ok = false;
    try {
      var b;
      for (b = 0; b < blockCount; b++) {
        var off = readU32(d, HEADER_BYTES + 4 * b);
        if (b === 0 && off !== 0) return null;
        if (b > 0 && off <= offsets[b - 1]) return null;
        if (off >= payloadBytes) return null;
        offsets[b] = off;
      }

      var payloadStart = HEADER_BYTES + 4 * blockCount;
      payload = new U8(payloadBytes);
      if (payloadBytes > 0) {
        apply(taSet, payload, [new U8(apply(taBufferGet, d, []), payloadStart, payloadBytes), 0]);
      }

      firstKeys = new U8(blockCount * FIRST_STRIDE);
      prev = new U8(MAX_KEY_LEN);
      cur = new U8(MAX_KEY_LEN);
      var prevLen = 0;
      var havePrev = false;

      for (b = 0; b < blockCount; b++) {
        var start = offsets[b];
        var end = b + 1 < blockCount ? offsets[b + 1] : payloadBytes;
        var remaining = count - b * BLOCK_SIZE;
        var records = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        var pos = start;
        for (var r = 0; r < records; r++) {
          if (end - pos < 2) return null;
          var prefix = payload[pos];
          var suffix = payload[pos + 1];
          pos += 2;
          if (r === 0 && prefix !== 0) return null;
          if (prefix > MAX_KEY_LEN || suffix > MAX_KEY_LEN) return null;
          if (prefix > prevLen) return null;
          if (suffix < 1) return null;
          var length = prefix + suffix;
          if (length < 1 || length > MAX_KEY_LEN) return null;
          if (suffix > end - pos) return null;
          var j;
          for (j = 0; j < prefix; j++) cur[j] = prev[j];
          for (j = 0; j < suffix; j++) {
            var byte = payload[pos + j];
            if (byte >= LETTER_KEYS) return null;
            cur[prefix + j] = byte;
          }
          pos += suffix;
          if (havePrev) {
            if (compareBuffers(prev, prevLen, cur, length) >= 0) return null;
            if (r > 0 && commonPrefix(prev, prevLen, cur, length) !== prefix) return null;
          }
          if (r === 0) {
            var base = b * FIRST_STRIDE;
            firstKeys[base] = length;
            for (j = 0; j < length; j++) firstKeys[base + 1 + j] = cur[j];
          }
          for (j = 0; j < length; j++) prev[j] = cur[j];
          prevLen = length;
          havePrev = true;
        }
        if (pos !== end) return null;
      }
      ok = true;
      return { count: count, blockCount: blockCount, offsets: offsets, payload: payload, firstKeys: firstKeys };
    } finally {
      wipe(prev);
      wipe(cur);
      if (!ok) {
        wipe(offsets);
        wipe(payload);
        wipe(firstKeys);
      }
    }
  }

  // Reads the key into a bounded local copy and validates it. Each length and
  // element is read only once. Returns the snapshot length, or 0 if the key is
  // invalid.
  function snapshotKey(keys, out) {
    if (keys === null || typeof keys !== 'object') return 0;
    var len, i, v;
    var isU8 = false;
    try { isU8 = apply(taTagGet, keys, []) === 'Uint8Array'; } catch (e) { isU8 = false; }
    if (isU8) {
      len = apply(taLengthGet, keys, []);
      if (typeof len !== 'number' || len < 1 || len > MAX_KEY_LEN) return 0;
      for (i = 0; i < len; i++) {
        v = keys[i];
        if (typeof v !== 'number' || v > MAX_KEY_LEN) return 0;
        out[i] = v;
      }
      return len;
    }
    if (!isArray(keys)) return 0;
    len = keys.length;
    if (typeof len !== 'number' || len < 1 || len > MAX_KEY_LEN || (len | 0) !== len) return 0;
    for (i = 0; i < len; i++) {
      v = keys[i];
      if (typeof v !== 'number' || !(v >= 0 && v <= MAX_KEY_LEN) || (v | 0) !== v) return 0;
      out[i] = v;
    }
    return len;
  }

  function makeInstance(st) {
    var disposed = false;
    var count = st.count;
    var blockCount = st.blockCount;
    var offsets = st.offsets;
    var payload = st.payload;
    var firstKeys = st.firstKeys;
    st = null;

    function contains(keys) {
      var key = null;
      var buf = null;
      try {
        if (disposed || count === 0) return false;
        key = new U8(MAX_KEY_LEN);
        var klen = snapshotKey(keys, key);
        if (klen === 0) return false;
        // Accessors in the input could have disposed this instance.
        if (disposed) return false;
        var fk = firstKeys, offs = offsets, pl = payload, n = count;

        var lo = 0, hi = blockCount;
        while (lo < hi) {
          var mid = (lo + hi) >>> 1;
          var base = mid * FIRST_STRIDE;
          var flen = fk[base];
          var common = flen < klen ? flen : klen;
          var cmp = 0;
          for (var i = 0; i < common; i++) {
            var a = fk[base + 1 + i];
            if (a !== key[i]) { cmp = a < key[i] ? -1 : 1; break; }
          }
          if (cmp === 0 && flen !== klen) cmp = flen < klen ? -1 : 1;
          if (cmp <= 0) lo = mid + 1; else hi = mid;
        }
        if (lo === 0) return false;
        var block = lo - 1;
        var remaining = n - block * BLOCK_SIZE;
        var records = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        var pos = offs[block];
        buf = new U8(MAX_KEY_LEN);
        for (var r = 0; r < records; r++) {
          var prefix = pl[pos];
          var suffix = pl[pos + 1];
          pos += 2;
          for (var j = 0; j < suffix; j++) buf[prefix + j] = pl[pos + j];
          pos += suffix;
          var c = compareBuffers(buf, prefix + suffix, key, klen);
          if (c === 0) return true;
          if (c > 0) return false;
        }
        return false;
      } catch (e) {
        return false;
      } finally {
        wipe(key);
        wipe(buf);
      }
    }

    function dispose() {
      if (disposed) return;
      disposed = true;
      wipe(payload);
      wipe(offsets);
      wipe(firstKeys);
      payload = null;
      offsets = null;
      firstKeys = null;
      count = 0;
      blockCount = 0;
    }

    function stats() {
      if (disposed) {
        return freeze({ keyCount: 0, blockCount: 0, payloadBytes: 0, indexBytes: 0 });
      }
      return freeze({
        keyCount: count,
        blockCount: blockCount,
        payloadBytes: payload.length,
        indexBytes: offsets.length * 4 + firstKeys.length
      });
    }

    return freeze({ contains: contains, dispose: dispose, stats: stats });
  }

  function loadLexicon(bytes) {
    var st = null;
    var own = null;
    try {
      own = snapshotInput(bytes);
      if (own !== null) st = parseOwned(own);
    } catch (e) {
      st = null;
    } finally {
      wipe(own);
      own = null;
    }
    if (st === null) throw new TypeErrorCtor(MSG);
    return makeInstance(st);
  }

  var api = freeze({ loadLexicon: loadLexicon });

  if (typeof module === 'object' && module !== null && typeof module.exports === 'object') {
    module.exports = api;
  } else if (root) {
    Object.defineProperty(root, 'RaskladLexicon', {
      value: api, writable: false, enumerable: false, configurable: true
    });
  }
})(typeof globalThis !== 'undefined' ? globalThis : (typeof self !== 'undefined' ? self : undefined));
