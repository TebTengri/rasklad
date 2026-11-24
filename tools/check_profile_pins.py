#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""SYS-256: refuse to package a profile the addon would refuse to load.

Reads the build/ pins (path, SHA-256, bytes) from makeProductionPins() in
src/profileverify.cpp, so there is no second copy of them, and checks a staged
profile directory against them. Also checks the marks the verifier requires:
the RSKX container on the model and the word list, the Rasklad header on each
control .aff.

usage: check_profile_pins.py <profile dir with build/... paths>
"""
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PIN = re.compile(r'\{QStringLiteral\("(build/[A-Za-z0-9._/-]+)"\),\s*QStringLiteral\("([0-9a-f]{64})"\),\s*(\d+)\}')
HEADER = re.compile(r'kControlAffHeader\[\] =\s*((?:"(?:[^"\\]|\\.)*"\s*)+);')


def production_pins(source=None):
    text = (source or ROOT / 'src/profileverify.cpp').read_text()
    start = text.index('ProfilePins makeProductionPins()')
    return {rel: (sha, int(size)) for rel, sha, size in PIN.findall(text[start:text.index('\n}\n', start)])}


def control_header():
    block = HEADER.search((ROOT / 'src/profileverify.h').read_text()).group(1)
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
    return ''.join(p.encode().decode('unicode_escape') for p in parts).encode()


def check(profile_dir):
    profile_dir = Path(profile_dir)
    errors = []
    pins = production_pins()
    if len(pins) < 8:
        errors.append('production pins not found')
    header = control_header()
    for rel, (sha, size) in sorted(pins.items()):
        path = profile_dir / rel
        if path.is_symlink() or not path.is_file():
            errors.append(f'missing: {rel}')
            continue
        blob = path.read_bytes()
        if len(blob) != size or hashlib.sha256(blob).hexdigest() != sha:
            errors.append(f'does not match its pin: {rel}')
        if rel.endswith(('.rskn', '.rskp')) and not blob.startswith(b'RSKX'):
            errors.append(f'no embedded notices: {rel}')
        if '/control/' in rel and not blob.startswith(header):
            errors.append(f'control .aff not marked: {rel}')
    return errors


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    errors = check(sys.argv[1])
    for e in errors:
        print(e, file=sys.stderr)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
