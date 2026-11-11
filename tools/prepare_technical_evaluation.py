#!/usr/bin/env python3
"""Prepare frozen experiment004 cases offline from hash-pinned Markdown.

Developer data preparation, not a runtime input parser. Source text is never
printed. Outputs a NEW TSV under build and updates only the manifest cases ref.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile

TRIM = '.!?;:,()[]{}"«»“”„'
RU = 'ёйцукенгшщзхъфывапролджэячсмитьбю'
US = "`qwertyuiop[]asdfghjkl;'zxcvbnm,."
SHIFT = '~QWERTYUIOP{}ASDFGHJKL:"ZXCVBNM<>'
TO_RU = dict(zip(US + SHIFT, RU + RU.upper()))
TO_US = {v: k for k, v in TO_RU.items()}
EN_ALPHA = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ')
RU_ALPHA = set(RU + RU.upper())
FENCE = re.compile(r'^ {0,3}(`{3,}|~{3,})')
RUN = re.compile(r'`+')
PROTOCOL_SHA = '9e1c9a17f9eb1ca1eb1f19643ae987bf98e2ed610611d12aaf2168964cf603e2'


def inline_spans(line):
    """Pair equal backtick runs on this line, with no rendering or execution."""
    position = 0
    while position < len(line):
        opening = RUN.search(line, position)
        if not opening:
            yield 'valid', line[position:]
            return
        closing = next((m for m in RUN.finditer(line, opening.end())
                        if len(m[0]) == len(opening[0])), None)
        if closing is None:
            yield 'valid', line[position:]
            return
        yield 'valid', line[position:opening.start()]
        yield 'protected', line[opening.end():closing.start()]
        position = closing.end()


def spans(markdown):
    fence = None
    for line in markdown.splitlines():
        match = FENCE.match(line)
        if fence is not None:
            if match and match[1][0] == fence[0] and len(match[1]) >= len(fence):
                fence = None
            else:
                yield 'protected', line
        elif match:
            fence = match[1]
        else:
            yield from inline_spans(line)


def utf16_length(text):
    return len(text.encode('utf-16-le')) // 2


def rows_for_document(index, language, text):
    for role, span in spans(text):
        for token in span.split():
            token = token.strip(TRIM)
            if not token:
                continue
            if utf16_length(token) > 4096 or any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in token):
                raise ValueError('invalid token bound or control')
            layout = 'ru' if any(c in RU_ALPHA for c in token) else 'us'
            yield (str(index), language, role, layout, token, token)
            alphabet = set(token)
            if role == 'valid' and 1 <= len(token) <= 32 and (alphabet <= EN_ALPHA or alphabet <= RU_ALPHA):
                mapping = TO_RU if layout == 'us' else TO_US
                wrong = ''.join(mapping[c] for c in token)
                back = TO_US if layout == 'us' else TO_RU
                if ''.join(back[c] for c in wrong) != token:
                    raise ValueError('round-trip mismatch')
                yield (str(index), language, 'wrong', 'ru' if layout == 'us' else 'us', wrong, token)


def safe_build_file(root, relative):
    if not isinstance(relative, str) or not relative.startswith('build/'):
        raise ValueError('path outside build')
    path = root / relative
    if (root / 'build').is_symlink() or path.resolve() != path.absolute() or not path.is_file():
        raise ValueError('path unavailable or symlinked')
    return path


def checked_bytes(path, size, sha, maximum):
    with path.open('rb') as stream:
        raw = stream.read(maximum + 1)
    if len(raw) > maximum or len(raw) != size or hashlib.sha256(raw).hexdigest() != sha:
        raise ValueError('size or checksum mismatch')
    return raw


def prepare(root, manifest_path):
    protocol = (root / 'data/experiment-004-protocol.json').read_bytes()
    if hashlib.sha256(protocol).hexdigest() != PROTOCOL_SHA:
        raise ValueError('protocol not frozen version')
    manifest = json.loads(manifest_path.read_text())
    if (manifest.get('status') != 'approved-local-evaluation'
            or manifest.get('scope') != 'local-evaluation-only'
            or manifest.get('release_approved') is not False
            or manifest.get('protocol_sha256') != PROTOCOL_SHA
            or manifest.get('cases') is not None):
        raise ValueError('manifest status or preparation already done')
    documents = manifest['documents']
    if len(documents) != 40 or [d['index'] for d in documents] != list(range(40)):
        raise ValueError('document selection mismatch')
    # Recompute path ranking against the separately cached tree metadata.
    for lang in ['en', 'ru']:
        tree = root / f'build/experiment-004-data/{lang}-tree.json'
        raw = tree.read_bytes()
        if hashlib.sha256(raw).hexdigest() != manifest['repositories'][lang]['treeSha256']:
            raise ValueError('tree metadata changed')
        prefix = 'src/' if lang == 'en' else 'rustbook-ru/src/'
        candidates = [x for x in json.loads(raw)['tree'] if x['type'] == 'blob'
                      and re.fullmatch(re.escape(prefix) + r'ch[0-9]{2}[^/]*[.]md', x['path'])]
        selected = sorted(candidates, key=lambda x: (
            hashlib.sha256(b'rasklad-SYS-236-doc-v1\0' + x['path'].encode()).digest(), x['path']))[:20]
        actual = [d for d in documents if d['language'] == lang]
        if [d['upstream_path'] for d in actual] != [x['path'] for x in selected]:
            raise ValueError('ranked selection mismatch')
        if [d['git_blob_sha1'] for d in actual] != [x['sha'] for x in selected]:
            raise ValueError('blob references differ')
    output = root / 'build/experiment-004-data/cases.tsv'
    if output.exists() or output.resolve() != output.absolute():
        raise ValueError('output exists or is symlinked')
    result = bytearray()
    count = 0
    for d in documents:
        source = safe_build_file(root, d['path'])
        raw = checked_bytes(source, d['bytes'], d['sha256'], 1048576)
        if hashlib.sha1(f'blob {len(raw)}\0'.encode() + raw).hexdigest() != d['git_blob_sha1']:
            raise ValueError('blob content mismatch')
        for row in rows_for_document(d['index'], d['language'], raw.decode('utf-8')):
            result += ('\t'.join(row) + '\n').encode('utf-8')
            count += 1
            if count > 500000 or len(result) > 64 * 1024 * 1024:
                raise ValueError('case bound exceeded')
    output.write_bytes(result)
    manifest['cases'] = {'path': str(output.relative_to(root)), 'sha256': hashlib.sha256(result).hexdigest(),
                         'bytes': len(result), 'rows': count}
    with tempfile.NamedTemporaryFile(dir=manifest_path.parent, mode='w', delete=False) as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
        temporary = Path(stream.name)
    os.replace(temporary, manifest_path)
    print(json.dumps(manifest['cases']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    manifest = Path(args.manifest).absolute()
    if manifest != root / 'data/evaluation-004-sources.json':
        raise ValueError('unexpected manifest location')
    prepare(root, manifest)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, UnicodeError, TypeError):
        raise SystemExit('prepare_technical_evaluation: validation failed')
