#!/usr/bin/env python3
"""Explicit offline PFX/SFX ablation for approved local affix files.

Other bytes, including comments and line endings, are retained. No network,
word output, dictionary expansion or modifications of the original file.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / 'build'
    if build.is_symlink():
        raise ValueError('build must not be a symlink')
    build = build.resolve()
    source = Path(args.input).resolve()
    target = Path(args.output).resolve()
    if not source.is_relative_to(build) or not target.is_relative_to(build) or target.exists():
        raise ValueError('source and new destination must be inside build')
    with source.open('rb') as stream:
        raw = stream.read(1024 * 1024 + 1)
    if len(raw) > 1024 * 1024 or raw.count(b'\n') > 250000:
        raise ValueError('input limit exceeded')
    raw.decode('utf-8')
    kept = []
    removed = 0
    for row in raw.splitlines(keepends=True):
        fields = row.split(None, 1)
        if fields and fields[0] in (b'PFX', b'SFX'):
            removed += 1
        else:
            kept.append(row)
    if not removed:
        raise ValueError('no PFX/SFX rows in source')
    result = b''.join(kept)
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as stream:
            temp = Path(stream.name)
            stream.write(result)
        os.replace(temp, target)
    finally:
        if temp is not None:
            temp.unlink(missing_ok=True)
    print(json.dumps({'removedRows': removed, 'sourceBytes': len(raw), 'controlBytes': len(result),
                      'sourceSha256': hashlib.sha256(raw).hexdigest(),
                      'controlSha256': hashlib.sha256(result).hexdigest()}))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, UnicodeError):
        raise SystemExit('prepare_affix_control: input or output validation failed')
