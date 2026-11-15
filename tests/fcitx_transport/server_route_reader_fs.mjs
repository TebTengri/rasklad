// SPDX-License-Identifier: MIT
// Run only in the closed unit namespace with /run/lab on private tmpfs.
import assert from 'node:assert/strict';
import {writeFileSync,chmodSync,unlinkSync,symlinkSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {createServerRouteWitness} from '/lab/server-route-witness.mjs';
const path='/run/lab/server-route-witness.bin';
const epoch=process.argv.includes('--epoch'),bytes=epoch?112:80;
const v=[0x52535731,1,2,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,0];
if(epoch){v[1]=2;v.push(1,1,0,0,0,1,1,0);}
const buffer=Buffer.alloc(bytes);v.forEach((n,i)=>buffer.writeUInt32LE(n,i*4));
const write=(b=buffer)=>{writeFileSync(path,b,{mode:0o600});chmodSync(path,0o600);};
write();let w=createServerRouteWitness({grabEpoch:epoch});assert.equal(await w.begin(),true);
assert.equal((await w.sample()).complete,true);assert.equal(await w.begin(),false);unlinkSync(path);
for(const mode of [0o644,0o400,0o660]){write();chmodSync(path,mode);w=createServerRouteWitness({grabEpoch:epoch});assert.equal(await w.begin(),false);unlinkSync(path);}
for(const size of [0,bytes-1,bytes+1]){write(Buffer.alloc(size));w=createServerRouteWitness({grabEpoch:epoch});assert.equal(await w.begin(),false);unlinkSync(path);}
writeFileSync('/run/lab/target',buffer,{mode:0o600});symlinkSync('/run/lab/target',path);
w=createServerRouteWitness({grabEpoch:epoch});assert.equal(await w.begin(),false);unlinkSync(path);
execFileSync('/usr/bin/mkfifo',[path]);w=createServerRouteWitness({grabEpoch:epoch});assert.equal(await w.begin(),false);unlinkSync(path);
w=createServerRouteWitness({grabEpoch:epoch});assert.equal(await w.begin(),false);
