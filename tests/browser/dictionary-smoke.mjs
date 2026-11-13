// SPDX-License-Identifier: MIT
// Explicit isolated developer fixture; input is prepared by the pinned parent.
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import assert from 'node:assert/strict';
import { startIsolatedChromium } from './isolated-chromium.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const MAX_STDIN = 16 * 1024 * 1024;
const LANGS = ['en', 'ru'];
const B64 = /^[A-Za-z0-9+/]*={0,2}$/;

async function readStdin() {
  const chunks = [];
  let total = 0;
  for await (const chunk of process.stdin) {
    total += chunk.length;
    if (total > MAX_STDIN) throw new Error('stdin');
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks, total).toString('utf8'));
}

function validate(input) {
  assert.ok(input && typeof input === 'object' && !Array.isArray(input));
  assert.deepEqual(Object.keys(input).sort(),['nativeVersion','profiles','schema']);
  assert.equal(input.schema, 1);
  assert.equal(input.nativeVersion, '1.7.5');
  const p = input.profiles;
  assert.ok(p && typeof p === 'object');
  assert.deepEqual(Object.keys(p).sort(), [...LANGS].sort());
  for (const lang of LANGS) {
    const prof = p[lang];
    assert.deepEqual(Object.keys(prof).sort(),['affBase64','cases','controlBase64','dicBase64']);
    for (const k of ['affBase64', 'dicBase64', 'controlBase64']) {
      assert.ok(typeof prof[k] === 'string' && prof[k].length > 0 && B64.test(prof[k]));
    }
    assert.ok(Array.isArray(prof.cases) && prof.cases.length >= 1 && prof.cases.length <= 256);
    for (const c of prof.cases) {
      assert.ok(typeof c.token === 'string' && c.token.length > 0 && c.token.length <= 32);
      assert.ok(typeof c.full === 'boolean' && typeof c.control === 'boolean');
    }
  }
}

function lockedBase64(entry) {
  assert.ok(entry && typeof entry.path === 'string' && /^[0-9a-f]{64}$/.test(entry.sha256));
  const bytes = readFileSync(resolve(root, entry.path));
  assert.equal(createHash('sha256').update(bytes).digest('hex'), entry.sha256);
  return bytes.toString('base64');
}

async function pageMain(cfg) {
  let networkAttempts = 0;
  const blocked = () => { networkAttempts += 1; throw new Error('network-disabled'); };
  globalThis.fetch = blocked;
  globalThis.XMLHttpRequest = function XMLHttpRequest() { blocked(); };
  const from64 = (s) => Uint8Array.from(atob(s), (ch) => ch.charCodeAt(0));
  const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
  const S = globalThis.RaskladSpell;

  const t0 = performance.now();
  const { default: factory } = await import('data:text/javascript;base64,' + cfg.loader);
  const m = await factory({
    instantiateWasm(imports, receive) {
      const module = new WebAssembly.Module(from64(cfg.wasm));
      const instance = new WebAssembly.Instance(module, imports);
      receive(instance, module);
      return instance.exports;
    },
    locateFile() { throw new Error('locate-disabled'); },
    print() {},
    printErr() {},
  });
  const moduleInitMs = performance.now() - t0;

  let controlMatches = 0;
  const profile = {};
  for (const lang of cfg.langs) {
    const p = cfg.profiles[lang];
    const aff = from64(p.affBase64);
    if (eq(S.removeAffixRows(aff), from64(p.controlBase64))) controlMatches += 1;
    profile[lang] = { aff, dic: from64(p.dicBase64) };
  }
  const t1 = performance.now();
  const engines = S.createEngines(m, profile);
  const dictionaryInitMs = performance.now() - t1;

  let mismatches = 0;
  let spellCases = 0, fullHits = 0, affixOnly = 0;
  const flat = [];
  for (const lang of cfg.langs) {
    for (const c of cfg.profiles[lang].cases) {
      spellCases += 1;
      if(c.full)fullHits++;
      if(c.full && !c.control)affixOnly++;
      if (engines.full(lang, c.token) !== c.full) mismatches += 1;
      if (engines.control(lang, c.token) !== c.control) mismatches += 1;
      flat.push([lang, c.token]);
    }
  }

  const BATCH = 50, CALLS = 2000, means = [];
  let lookupCalls = 0;
  for (let b = 0; b < CALLS / BATCH; b += 1) {
    const start = performance.now();
    for (let j = 0; j < BATCH; j += 1, lookupCalls += 1) {
      const [lang, token] = flat[lookupCalls % flat.length];
      (lookupCalls % 2 === 0 ? engines.full : engines.control)(lang, token);
    }
    means.push((performance.now() - start) / BATCH);
  }
  means.sort((a, b) => a - b);
  const pct = (q) => means[Math.min(means.length - 1, Math.floor(q * means.length))];

  const ready = engines.stats();
  engines.dispose();
  const after = engines.stats();
  let revived = true;
  try { engines.full(cfg.langs[0], flat[0][1]); } catch { revived = false; }
  const final = engines.stats();

  return {
    spellCases, fullHits, affixOnly, controlMatches, mismatches, lookupCalls,
    heapBytes: ready.heapBytes, engineCount: ready.engineCount,
    disposedOk: after.status === 'disposed' && after.engineCount === 0 && !revived
      && final.status === 'disposed' && final.engineCount === 0,
    moduleInitMs, dictionaryInitMs,
    lookupBatchMeanP50Ms: pct(0.5), lookupBatchMeanP95Ms: pct(0.95), networkAttempts,
  };
}

async function main() {
  const input = await readStdin();
  validate(input);
  const lock = JSON.parse(readFileSync(resolve(root, 'tools/browser_spell/source-lock.json'), 'utf8'));
  assert.equal(lock.version, '1.7.5');
  assert.equal(lock.release_approved, false);
  const cfg = { loader: lockedBase64({path:lock.loader_path,sha256:lock.loader_sha256}), wasm: lockedBase64({path:lock.wasm_path,sha256:lock.wasm_sha256}), langs: LANGS, profiles: input.profiles };
  const spellSource = readFileSync(resolve(root, 'browser/spell.js'), 'utf8');

  const browser = await startIsolatedChromium(root, 60000);
  try {
    const { product } = await browser.call('Browser.getVersion');
    const { targetId } = await browser.call('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await browser.call('Target.attachToTarget', { targetId, flatten: true });
    await browser.call('Runtime.enable', {}, sessionId);
    const evaluate = async (expression) => {
      const r = await browser.call('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }, sessionId);
      if (r.exceptionDetails) throw new Error('evaluate');
      return r.result.value;
    };
    await evaluate(`${spellSource}\n;void 0`);
    const res = await evaluate(`(${pageMain.toString()})(${JSON.stringify(cfg)})`);

    assert.equal(res.spellCases, LANGS.reduce((n, l) => n + input.profiles[l].cases.length, 0));
    assert.equal(res.controlMatches, LANGS.length);
    assert.equal(res.mismatches, 0);
    assert.ok(res.fullHits>0 && res.affixOnly>0);
    assert.equal(res.lookupCalls, 2000);
    assert.equal(res.engineCount, 4);
    assert.ok(Number.isFinite(res.heapBytes) && res.heapBytes > 0 && res.heapBytes <= lock.maximum_memory_bytes);
    assert.equal(res.disposedOk, true);
    assert.equal(res.networkAttempts, 0);

    const r3 = (x) => Math.round(x * 1000) / 1000;
    return {
      scope: 'dictionary-engine-smoke-only', passed: true, nativeVersion: input.nativeVersion,
      browser: product, spellCases: res.spellCases, controlMatches: res.controlMatches,
      fullHits:res.fullHits,affixOnly:res.affixOnly, mismatches: res.mismatches, heapBytes: res.heapBytes, engineCount: res.engineCount,
      moduleInitMs: r3(res.moduleInitMs), dictionaryInitMs: r3(res.dictionaryInitMs),
      lookupBatchMeanP50Ms: r3(res.lookupBatchMeanP50Ms), lookupBatchMeanP95Ms: r3(res.lookupBatchMeanP95Ms),
      lookupCalls: 2000, networkAttempts: res.networkAttempts, releaseApproved: false,
    };
  } finally {
    await browser.close();
  }
}

main().then(result => process.stdout.write(JSON.stringify(result)+'\n')).catch(() => {
  process.stdout.write(JSON.stringify({ passed: false, error: 'dictionary-smoke-failed' }) + '\n');
  process.exitCode = 1;
});
