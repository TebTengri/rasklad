#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""SYS-256: the release profile, made from the reviewed experiment profile.

No training and no download. The dictionaries and .aff stay byte for byte. Three
things change, so that every distributed copy carries its notices and marks:
  - each control .aff gets the Hunspell comment header kControlAffHeader
    (src/profileverify.h) in front of its unchanged bytes;
  - detector-model.rskn and exact-full-lexicons.rskp are wrapped in the RSKX
    container (wrapNoticeContainer): the full en_US and ru_RU notices, then the
    unchanged RSKN/RSKP payload;
  - artifact-metadata.json records the new sizes and hashes.

usage: make_release_profile.py --from <profile root> --out <new dir inside build/>
The source must match the pins below (the reviewed profile); the output directory
must not exist. Prints the changed files' new pins as JSON.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The reviewed experiment profile (src/profileverify.cpp before SYS-256).
SOURCE_PINS = {
    'build/experiment-data/en.dic': ('f0b1a234bd178bdd01875b2a392a9647f888b8fe879f79c52aae62c2759b3647', 551762),
    'build/experiment-data/ru.dic': ('f6047416a0204adbecf3a451b874ec8a97ee37e2cbc714466ef04d8dbcc0d6fc', 3473191),
    'build/experiment-003-data/en.aff': ('e746c882dd6f303c2c46e7452804b9201115a6942cfeb15f18f8edf774d2e24e', 3205),
    'build/experiment-003-data/ru.aff': ('38ce7d4af78e211e9bafe4bf7e3d6a2c420591136cb738ec6648f8fdf6524cd7', 71236),
    'build/experiment-003-data/control/en.aff': ('60fde13d7b4856a2dcb4a70eb9320a7288e38f3cd11ca6a40509a1d22e81f08f', 1460),
    'build/experiment-003-data/control/ru.aff': ('097549718b2cc53e0c597abb01a467fb8161ac9b3cac35287107717e6e71dd19', 162),
    'build/experiment-002-artifacts/detector-model.rskn': ('e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8', 88290),
    'build/experiment-002-artifacts/exact-full-lexicons.rskp': ('3bdd740d4b39bf2b5fe361e0d3c670187563ec59f41913db3525340a30887871', 1200379),
    'build/experiment-002-artifacts/artifact-metadata.json': ('9a55205497c9cea58a157b83a3eeca3d66be899679265888443d1f18fee79612', 1827),
    'build/experiment-002-artifacts/MODIFICATIONS.txt': ('e2a4d9b6956c3210d0f60cf65432c7f8bfe3b891852f39832e46e4e6b3408af5', 182),
    'build/experiment-002-artifacts/en_US.NOTICE.txt': ('168b4c01cb841f766a72f310b065c04d09cff46376c37a81d799593ce751c371', 15732),
    'build/experiment-002-artifacts/ru_RU.NOTICE.txt': ('262af2f6ad70a61e5ee1332ff44fa8ee50edca819cf33207d8ad6ba6a0c9be52', 1886),
}

# Must equal kControlAffHeader in src/profileverify.h (checked by tests/packaging).
CONTROL_AFF_HEADER = (
    b'# Modified by Rasklad: the PFX and SFX rules of the upstream .aff are removed.\n'
    b'# Not an upstream file. See /usr/share/licenses/rasklad-fcitx5/DATA-NOTICE.txt\n')
MAX_NOTICE = 64 * 1024  # kMaxEmbeddedNoticeBytes


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def embedded_notice(what):
    en = (ROOT / 'data/notices/en_US.README.txt').read_bytes()
    ru = (ROOT / 'data/notices/ru_RU.README.txt').read_bytes()
    head = (
        f'Rasklad {what}: data derived from the en_US and ru_RU Hunspell dictionaries\n'
        'of LibreOffice/dictionaries commit c0865a85eb67dc377f2cf8e8fe2c4fa9e53f0dcb.\n'
        'Modified by Rasklad; not an upstream file, not endorsed by the dictionary authors.\n'
        "Rasklad's MIT license does not cover this data. The full notices of both\n"
        'dictionaries follow. The UKACD document is in\n'
        '/usr/share/licenses/rasklad-fcitx5/UKACD-readme.txt.\n\n'
        '===== en_US: full notice =====\n').encode()
    notice = head + en + b'\n===== ru_RU: full notice =====\n' + ru
    notice.decode('utf-8')  # strict UTF-8, as the loader requires
    if not 0 < len(notice) <= MAX_NOTICE or b'\0' in notice:
        raise SystemExit('embedded notice out of bounds')
    return notice


def wrap(notice, payload):
    return b'RSKX' + struct.pack('<I', len(notice)) + notice + payload


def build_profile(data):
    """Returns the release profile (relative path -> bytes) for the reviewed one."""
    data = dict(data)
    for lang in ('en', 'ru'):
        rel = f'build/experiment-003-data/control/{lang}.aff'
        data[rel] = CONTROL_AFF_HEADER + data[rel]
    model_rel = 'build/experiment-002-artifacts/detector-model.rskn'
    stem_rel = 'build/experiment-002-artifacts/exact-full-lexicons.rskp'
    data[model_rel] = wrap(embedded_notice('detector model'), data[model_rel])
    data[stem_rel] = wrap(embedded_notice('word list'), data[stem_rel])

    meta_rel = 'build/experiment-002-artifacts/artifact-metadata.json'
    meta = json.loads(data[meta_rel])
    for key, rel, inner in (('model', model_rel, 'RSKN v1 (NgramModel::serialize)'),
                            ('exactFullLexicons', stem_rel,
                             'RSKP v1 pair of RSKF v1 front-coded lexicons, block size 16')):
        entry = meta['artifacts'][key]
        entry['sha256'] = sha256(data[rel])
        entry['bytes'] = len(data[rel])
        entry['format'] = f'RSKX v1 notice container around {inner}'
    data[meta_rel] = (json.dumps(meta, indent=4, sort_keys=True, ensure_ascii=False) + '\n').encode()
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--from', dest='source', required=True)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    source = Path(args.source).resolve()
    build = (ROOT / 'build').resolve()
    out = Path(args.out).absolute()
    if (ROOT / 'build').is_symlink() or not out.parent.resolve().is_relative_to(build) or out.exists():
        raise SystemExit("--out must be a new directory inside this project's build/")

    data = {}
    for rel, (digest, size) in SOURCE_PINS.items():
        path = source / rel
        if path.is_symlink() or not path.is_file():
            raise SystemExit(f'missing or not a regular file: {rel}')
        blob = path.read_bytes()
        if len(blob) != size or sha256(blob) != digest:
            raise SystemExit(f'not the reviewed profile: {rel}')
        data[rel] = blob

    data = build_profile(data)
    out.mkdir()
    for rel, blob in data.items():
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)
    pins = {rel: {'sha256': sha256(blob), 'bytes': len(blob)} for rel, blob in sorted(data.items())
            if sha256(blob) != SOURCE_PINS[rel][0]}
    json.dump(pins, sys.stdout, indent=2)
    print()


if __name__ == '__main__':
    main()
