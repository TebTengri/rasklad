#!/usr/bin/env python3
"""Summarize bounded synthetic input metadata; never accepts text/key payloads."""
import json
import math
import pathlib
import sys


def summarize(record):
    r = record['report']
    if r.get('scenario') not in ('fcitx-perf', 'fcitx-perf-native'):
        raise ValueError('unsupported measurement scenario')
    c = r['field_assertions_client_view']
    if r.get('overall_passed') is not True or c.get('timing_complete') is not True:
        raise ValueError('incomplete or failed fixture; do not publish optimistic latency')
    sent, seen = r['timing_sent_ns'], c['timing_observed_ns']
    if len(sent) != 60 or len(seen) != 60:
        raise ValueError('exactly 60 paired observations required')
    for seq in (sent, seen):
        if any(type(n) is not int or n <= 0 for n in seq):
            raise ValueError('invalid timestamp')
        if any(b <= a for a, b in zip(seq, seq[1:])):
            raise ValueError('missing, duplicate or reordered observation')
    if any(b < a or b - a > 10_000_000_000 for a, b in zip(sent, seen)):
        raise ValueError('invalid time interval')
    samples = sorted((b - a) / 1_000_000 for a, b in zip(sent, seen))
    result = {'scenario': r['scenario'], 'observations': len(samples),
              'editor_state_latency_ms': {label: round(samples[math.ceil(q * len(samples)) - 1], 4)
                  for label, q in [('p50', .5), ('p95', .95), ('p99', .99), ('max', 1)]}}
    resource = r['fixture_resources']
    hz = resource['clock_ticks_per_second']
    if type(hz) is not int or hz <= 0:
        raise ValueError('invalid CPU clock resolution')
    phases = resource['phases']
    stats = {'scope': 'private Fcitx daemon, not whole app', 'cpu_resolution_ms': 1000 / hz}
    for phase in ('idle', 'typing'):
        a, b = phases[phase + '-start'], phases[phase + '-end']
        if b['cpu_ticks'] < a['cpu_ticks'] or b['wall_ns'] <= a['wall_ns']:
            raise ValueError('invalid resource interval')
        stats[phase + '_cpu_ms'] = (b['cpu_ticks'] - a['cpu_ticks']) * 1000 / hz
        stats[phase + '_wall_ms'] = round((b['wall_ns'] - a['wall_ns']) / 1_000_000, 3)
    stats['final_rss_kib'] = resource['final']['rss_kib']
    stats['peak_rss_kib'] = resource['final']['peak_rss_kib']
    result['daemon_resources'] = stats
    return result


def main(paths):
    if not 1 <= len(paths) <= 32:
        raise ValueError('supply 1..32 local attempt JSON files')
    runs = []
    for name in paths:
        p = pathlib.Path(name)
        with p.open('rb') as f:
            raw = f.read(65537)
        if len(raw) > 65536:
            raise ValueError('oversized metadata file')
        runs.append(summarize(json.loads(raw)))
    print(json.dumps({'scope': 'synthetic editor state, not display presentation or real detector',
                      'quantile': 'nearest rank per run; p99 at n=60 equals maximum', 'runs': runs}, indent=2))


if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except (ValueError, KeyError, TypeError, OSError) as e:
        raise SystemExit('input-latency-report: ' + str(e))
