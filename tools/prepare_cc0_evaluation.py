#!/usr/bin/env python3
"""Explicit offline preparation of the reviewed Tatoeba CC0-only export.

No downloads, training or sentence output. Source URL and hashes belong to
 data/evaluation-002-sources.json. The timestamp is removed; text is retained.
"""
import argparse
import bz2
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--language', choices=['eng', 'rus'], required=True)
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    build_path = project / 'build'
    if build_path.is_symlink():
        raise ValueError('project build/ must not be a symlink')
    build = build_path.resolve()
    target = Path(args.output).resolve()
    if not target.is_relative_to(build) or target.exists():
        raise ValueError('output must be a new file inside project build/')
    with Path(args.input).open('rb') as source:
        compressed = source.read(4 * 1024 * 1024 + 1)
    if len(compressed) > 4 * 1024 * 1024:
        raise ValueError('compressed size limit exceeded')
    decoder = bz2.BZ2Decompressor()
    raw = decoder.decompress(compressed, max_length=32 * 1024 * 1024 + 1)
    if len(raw) > 32 * 1024 * 1024 or not decoder.eof or decoder.unused_data:
        raise ValueError('invalid or oversized compressed stream')
    record_lines = raw.count(b'\n') + (0 if raw.endswith(b'\n') else 1)
    if record_lines > 250000:
        raise ValueError('record count limit exceeded')
    rows = raw.decode('utf-8').splitlines()
    prepared = []
    ids = set()
    for row in rows:
        fields = row.split('\t')
        if len(fields) != 4:
            raise ValueError('expected four upstream TSV columns')
        sid, language, text, timestamp = fields
        if not re.fullmatch(r'[1-9][0-9]{0,19}', sid) or sid in ids:
            raise ValueError('invalid or duplicate sentence ID')
        ids.add(sid)
        if language != args.language or not text:
            raise ValueError('invalid language or empty sentence')
        if len(row.encode('utf-8')) > 8192 or len(text.encode('utf-16-le')) > 8192:
            raise ValueError('sentence size limit exceeded')
        if any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in text):
            raise ValueError('sentence contains control characters')
        if not re.fullmatch(r'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}', timestamp):
            raise ValueError('invalid technical timestamp')
        prepared.append('\t'.join(fields[:3]))
    if not prepared:
        raise ValueError('empty input')
    result = ('\n'.join(prepared) + '\n').encode('utf-8')
    target.parent.mkdir(parents=True, exist_ok=True)
    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as temp:
            temp_path = Path(temp.name)
            temp.write(result)
        os.replace(temp_path, target)
    finally:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)
    print(json.dumps({'rows': len(prepared), 'bytes': len(result),
                      'sha256': hashlib.sha256(result).hexdigest(),
                      'upstreamSha256': hashlib.sha256(raw).hexdigest()}))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, EOFError, UnicodeError):
        raise SystemExit('prepare_cc0_evaluation: input or output validation failed')
