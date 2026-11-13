/* Project license pending; see README.md */
'use strict';
// Entirely synthetic dictionary membership + numeric weights, not language accuracy.
// These tiny sets are NOT full/control Hunspell implementations.
const fixtureModel = RaskladScorer.loadModel(RaskladSyntheticModelBytes, RaskladPolicyData.modelSpec);
RaskladSyntheticModelBytes.fill(0);
delete globalThis.RaskladSyntheticModelBytes;
const full = { en: new Set(['hello']), ru: new Set(['привет', 'фисв', 'еуые']) };
const control = { en: new Set(['test']), ru: new Set(['привет', 'еуые']) };
const fixtureEvidence = RaskladEvidence.createEvidenceProvider({
  model: fixtureModel, policy: RaskladPolicyData, convert: RaskladConverter.convert,
  lookup: {
    full: (language, token) => full[language].has(token),
    control: (language, token) => control[language].has(token),
    stem: () => false
  }
});
globalThis.fixtureRouter = RaskladRouter.createRouter({
  policy: RaskladPolicyData, convert: RaskladConverter.convert, evidence: fixtureEvidence
});
globalThis.fixtureAdapter = RaskladBrowser.createAdapter({ document,
  decide: token => fixtureRouter.decideUs(token) });
for (const id of ['prose', 'protected', 'code'])
  fixtureAdapter.register(document.getElementById(id), { role: 'prose', layout: 'us' });
