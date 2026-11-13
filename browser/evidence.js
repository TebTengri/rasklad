/* SPDX-License-Identifier: MIT
 * Synchronous local evidence binding. Dictionary functions are trusted exact
 * full/control/stem engines, not a replacement implementation of Hunspell.
 */
(function(root) {
  'use strict';
  function createEvidenceProvider({model,policy,convert,lookup}={}) {
    if (!model || typeof model.score!=='function' || typeof model.decide!=='function' ||
        typeof convert!=='function' || !lookup || !policy || policy.schema!==1 ||
        !Number.isInteger(policy.minKeys) || !Number.isInteger(policy.maxKeys) ||
        policy.minKeys<4 || policy.maxKeys>32 || policy.minKeys>policy.maxKeys)
      throw new TypeError('Invalid evidence provider');
    const min=policy.minKeys,max=policy.maxKeys,minRaw=policy.modelSpec?.minStepRaw;
    if(!Number.isInteger(minRaw) || minRaw>=0 || minRaw < -1000000)throw new TypeError('Invalid score bounds');
    const score=model.score,decide=model.decide;
    const functions={};
    for(const name of ['full','control','stem']) {
      if(typeof lookup[name]!=='function')throw new TypeError('Missing exact dictionary engine');
      functions[name]=lookup[name];
    }
    const maps={};
    for(const layout of ['us','ru']) {
      const raw=policy.canonical?.[layout];
      if(!raw || typeof raw!=='object' || Array.isArray(raw))throw new TypeError('Invalid canonical map');
      const chars=Object.keys(raw);
      if(chars.length===0 || chars.length>256)throw new TypeError('Invalid canonical map');
      maps[layout]=new Map();
      for(const ch of chars) {
        if(ch.length!==1 || !Number.isInteger(raw[ch]) || raw[ch]<0 || raw[ch]>32)
          throw new TypeError('Invalid canonical map');
        maps[layout].set(ch,raw[ch]);
      }
    }
    function member(name,language,value) {
      const answer=Reflect.apply(functions[name],lookup,[language,value]);
      if(typeof answer!=='boolean')throw new TypeError('Unavailable dictionary evidence');
      return answer;
    }
    return function evidence(query) {
      try {
        if(!query || typeof query!=='object')return null;
        const token=query.token,layout=query.layout,inputKeys=query.keys;
        if(typeof token!=='string' || token.length<min || token.length>max ||
            (layout!=='us' && layout!=='ru') ||
            (!Array.isArray(inputKeys) && !(inputKeys instanceof Uint8Array)) || inputKeys.length!==token.length)
          return null;
        const keys=[];
        for(let i=0;i<token.length;i++) {
          const k=inputKeys[i];
          if(!Number.isInteger(k) || k<0 || k>32 || maps[layout].get(token[i])!==k)return null;
          keys.push(k);
        }
        Object.freeze(keys);
        const opposite=layout==='us'?'ru':'us';
        const candidate=convert(token,layout==='us'?'us-ru':'ru-us');
        if(typeof candidate!=='string' || candidate.length!==token.length)return null;
        for(let i=0;i<candidate.length;i++)if(maps[opposite].get(candidate[i])!==keys[i])return null;
        const observed=layout==='us'?'en':'ru',alternative=layout==='us'?'ru':'en';
        const value={};
        for(const name of ['full','control'])value[name]={
          observed:member(name,observed,token),alternative:member(name,alternative,candidate)};
        value.stem={observed:member('stem',observed,keys),alternative:member('stem',alternative,keys)};
        const scored=Reflect.apply(score,model,[keys]);
        if(!scored || scored.ok!==true || scored.keyCount!==keys.length || scored.steps!==keys.length+1 ||
            !Array.isArray(scored.raw) || scored.raw.length!==2 ||
            !scored.raw.every(value=>Number.isInteger(value) && value<=0 && value>=scored.steps*minRaw))return null;
        const action=Reflect.apply(decide,model,[scored,layout]);
        if(action!=='keep' && action!=='correct' && action!=='abstain')return null;
        value.model=action;
        return value;
      } catch { return null; }
    };
  }
  const api=Object.freeze({createEvidenceProvider});
  root.RaskladEvidence=api;
  if(typeof module!=='undefined')module.exports=api;
})(globalThis);
