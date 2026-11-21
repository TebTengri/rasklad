/* SPDX-License-Identifier: MIT
 * Owner-integrated plain-text adapter. No detector or global input backend.
 */
(function (root) {
  'use strict';
  function createAdapter({ document: doc, decide }) {
    if (!doc || typeof decide !== 'function') throw new TypeError('adapter arguments');
    const entries = new Map();
    let disposed = false;
    function register(field, options) {
      if (disposed || options?.role !== 'prose' || options?.layout !== 'us' ||
          field?.ownerDocument !== doc || field.tagName !== 'TEXTAREA' || entries.has(field))
        throw new TypeError('explicit owned US prose textarea required');
      const state = { generation: 0, composing: false, busy: false, disabled: false, suppressSpace: false, lastRange: null };
      function safe() {
        // Check role and hints before ever requesting field.value.
        const autocomplete = (field.getAttribute('autocomplete') || '').trim().toLowerCase();
        const hasCredential = field.form && Array.from(field.form.elements).some(control =>
          control.tagName === 'INPUT' && (control.type === 'password' ||
            /(?:^|\s)(?:current-password|new-password|one-time-code)(?:$|\s)/u.test(
              (control.getAttribute('autocomplete') || '').toLowerCase())));
        return !disposed && !state.disabled && field.isConnected &&
          field.ownerDocument === doc && doc.activeElement === field &&
          !field.disabled && !field.readOnly && field.spellcheck !== false &&
          (!autocomplete || autocomplete === 'on' || autocomplete === 'off') &&
          !field.closest('pre, code, [contenteditable], [inert], [aria-hidden="true"]') &&
          !hasCredential;
      }
      const change = () => { state.generation++; };
      const input = event => {
        change();
        if (event.inputType === 'historyUndo' || event.inputType === 'historyRedo') {
          state.suppressSpace = true;
          const range = state.lastRange;
          if (range && safe() && field.selectionStart === range.from && field.selectionEnd === range.end)
            field.setSelectionRange(range.end, range.end);
        } else if (!state.busy) { state.suppressSpace = false; state.lastRange = null; }
      };
      const start = () => { state.composing = true; change(); };
      const end = () => { state.composing = false; change(); };
      function before(event) {
        if (event.target !== field || !event.isTrusted || !event.cancelable || event.defaultPrevented ||
            event.inputType !== 'insertText' || event.data !== ' ' || event.isComposing ||
            state.composing || state.busy || state.suppressSpace || !safe()) return;
        let snapshot, cursor, generation, selectedFrom;
        function collapseOwnedSelection() {
          // Restore only our exact selection, without querying potentially protected text.
          if (selectedFrom !== undefined && field.selectionStart === selectedFrom &&
              field.selectionEnd === cursor) field.setSelectionRange(cursor, cursor);
        }
        try {
          snapshot = field.value;
          if (snapshot.length > 65536) return;
          cursor = field.selectionStart;
          if (cursor !== field.selectionEnd || cursor === 0 ||
              (cursor < snapshot.length && !/\s/u.test(snapshot[cursor]))) return;
          // Require whitespace boundaries, excluding URL/path/code fragments.
          const prefix = snapshot.slice(Math.max(0, cursor - 33), cursor);
          const match = /(?:^|\s)([A-Za-z]{4,32})$/u.exec(prefix);
          if (!match || (cursor > 32 && prefix.length === match[1].length)) return;
          const token = match[1], from = cursor - token.length;
          if (from > 0 && !/\s/u.test(snapshot[from - 1])) return;
          generation = state.generation;
          state.busy = true;
          let replacement;
          try { replacement = decide(token); } catch { state.disabled = true; return; }
          // No asynchronous result can own the document revision.
          if (replacement == null) return;
          if (typeof replacement !== 'string' || replacement.length !== token.length ||
              !/^[А-Яа-яЁё]{4,32}$/u.test(replacement)) { state.disabled = true; return; }
          const fresh = () => safe() && !state.composing && state.generation === generation &&
            field.value === snapshot && field.selectionStart === cursor && field.selectionEnd === cursor;
          if (!fresh()) return;
          selectedFrom = from;
          field.setSelectionRange(from, cursor);
          // setSelectionRange is native in an owned field. Recheck the exact range.
          if (!safe() || state.composing || state.generation !== generation || field.value !== snapshot ||
              field.selectionStart !== from || field.selectionEnd !== cursor) {
            event.preventDefault(); state.disabled = true; collapseOwnedSelection(); return;
          }
          let success = false;
          try { success = doc.execCommand('insertText', false, replacement + ' '); }
          catch { success = false; }
          const expected = snapshot.slice(0, from) + replacement + ' ' + snapshot.slice(cursor);
          if (!safe() || state.composing) {
            event.preventDefault(); state.disabled = true; collapseOwnedSelection(); return;
          }
          const actual = field.value;
          if (actual === expected && doc.activeElement === field &&
              field.selectionStart === cursor + 1 && field.selectionEnd === cursor + 1) {
            state.lastRange = { from, end: cursor };
            event.preventDefault();
            if (!success) state.disabled = true;
          } else if (actual === snapshot) {
            // A proven no-op can restore only the selection; native Space proceeds.
            collapseOwnedSelection();
            if (doc.activeElement !== field || state.generation !== generation ||
                field.selectionStart !== cursor || field.selectionEnd !== cursor) event.preventDefault();
            state.disabled = true;
          } else {
            // Never guess a rollback over synchronous application edits.
            event.preventDefault();
            state.disabled = true;
            collapseOwnedSelection();
          }
        } catch {
          state.disabled = true;
          if (selectedFrom !== undefined) { event.preventDefault(); try { collapseOwnedSelection(); } catch {} }
        }
        finally { state.busy = false; }
      }
      const listeners = [['beforeinput', before], ['input', input], ['change', change],
        ['focusin', change], ['focusout', change], ['compositionstart', start], ['compositionend', end]];
      for (const [name, fn] of listeners) field.addEventListener(name, fn);
      entries.set(field, { state, listeners });
      return () => unregister(field);
    }
    function unregister(field) {
      const entry = entries.get(field);
      if (!entry) return;
      entry.state.disabled = true;
      for (const [name, fn] of entry.listeners) field.removeEventListener(name, fn);
      entries.delete(field);
    }
    return Object.freeze({ register, unregister,
      status: field => entries.has(field) ? (entries.get(field).state.disabled ? 'disabled' : 'registered') : 'unregistered',
      dispose() { disposed = true; for (const field of entries.keys()) unregister(field); }
    });
  }
  root.RaskladBrowser = Object.freeze({ createAdapter });
  if (typeof module !== 'undefined') module.exports = root.RaskladBrowser;
})(globalThis);
