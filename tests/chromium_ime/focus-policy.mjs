// SPDX-License-Identifier: MIT
// Closed test-fixture provenance policy, NOT authentication of arbitrary DOM events.
export function selectEndEventPolicy({browserVersion,sourcesReviewed,control,correctedOnce}) {
  if(sourcesReviewed!==true||correctedOnce!==true||!/^Chrome\/[0-9.]+$/.test(browserVersion||''))return null;
  if(!control||control.trustedSourceStarts!==1||control.sourceEnds!==1||control.totalStarts!==1||control.totalEnds!==1)return null;
  if(control.trustedSourceEnds===1&&control.untrustedSourceEnds===0)return 'trusted_only';
  if(browserVersion==='Chrome/155.0.8059.84'&&control.trustedSourceEnds===0&&control.untrustedSourceEnds===1)return 'controlled_chromium_scoped_end';
  return null;
}
