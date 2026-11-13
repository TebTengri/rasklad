/* SPDX-License-Identifier: MIT */
'use strict';
// Synthetic metadata ONLY: exercises the shared policy, not language accuracy.
const pair = (observed=false, alternative=false) => ({observed, alternative});
globalThis.fixtureRouter = RaskladRouter.createRouter({
  policy: RaskladPolicyData,
  convert: RaskladConverter.convert,
  evidence: ({token}) => {
    if (token === 'ghbdtn') return {full:pair(false,true),control:pair(false,true),stem:pair(),model:'correct'};
    if (token === 'hello') return {full:pair(true,false),control:pair(),stem:pair(),model:'keep'};
    if (token === 'test') return {full:pair(false,true),control:pair(true,true),stem:pair(),model:'correct'};
    if (token === 'abcd') return {full:pair(false,true),control:pair(),stem:pair(),model:'abstain'};
    return null;
  }
});
globalThis.fixtureAdapter = RaskladBrowser.createAdapter({ document,
  decide: token => fixtureRouter.decideUs(token) });
for (const id of ['prose', 'protected', 'code'])
  fixtureAdapter.register(document.getElementById(id), { role: 'prose', layout: 'us' });
