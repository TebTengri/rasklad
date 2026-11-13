/* SPDX-License-Identifier: MIT */
'use strict';
// Synthetic evidence belongs ONLY to this fictional test fixture.
// No linguistic accuracy or shared C++ router integration is implied.
globalThis.fixtureAdapter = RaskladBrowser.createAdapter({ document,
  decide: token => token === 'ghbdtn' ? RaskladConverter.convert(token, 'us-ru') : null });
for (const id of ['prose', 'protected', 'code'])
  fixtureAdapter.register(document.getElementById(id), { role: 'prose', layout: 'us' });
