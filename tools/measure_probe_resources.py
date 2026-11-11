#!/usr/bin/env python3
"""Observe an explicitly supplied JSON probe child through /proc, without a shell.

Developer-only Linux tool. RSS includes shared pages and loader overhead. The
filesystem cache is not flushed; 2 ms samples can miss very short-lived peaks.
The child must emit one JSON object and no stderr. No corpus is read here.
"""
import subprocess,sys,time,pathlib,json,collections,statistics
start=time.perf_counter_ns()
p=subprocess.Popen(sys.argv[1:],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
peak=last=0;samples=0;tail=collections.deque(maxlen=100)
while p.poll() is None:
 try:
  fields={line.split(':',1)[0]:line.split(':',1)[1].strip() for line in pathlib.Path(f'/proc/{p.pid}/status').read_text().splitlines() if ':' in line}
  if 'VmHWM' in fields and 'VmRSS' in fields:
   peak=max(peak,int(fields['VmHWM'].split()[0]));last=int(fields['VmRSS'].split()[0]);samples+=1;tail.append(last)
 except (OSError,ValueError):pass
 time.sleep(0.002)
out,err=p.communicate();wall=time.perf_counter_ns()-start
if p.returncode or err:
 raise SystemExit('probe child failed')
result=json.loads(out)
print(json.dumps({'probe':result,'observer':{'childWallNs':wall,'peakRssKiB':peak or None,'lastObservedRssKiB':last or None,'lateLoopMedianRssKiB':statistics.median(tail) if tail else None,'samples':samples,'method':'/proc/<actual predictor child PID>/status VmHWM and VmRSS; 2ms sampling; no supervisor RSS','coldScope':'fresh process, filesystem cache not flushed'}}))
