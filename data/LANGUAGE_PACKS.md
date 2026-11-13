# Offline language dictionary packages

One locale is one data-only directory. Packages contain raw UTF-8 Hunspell
`.aff`/`.dic`, exact full notices, a modification record and canonical manifest.
There are no scripts, native libraries, URLs to fetch at runtime or installer hooks.
Nothing is added to system dictionaries or the user's browser profile.

The trusted, reviewer-owned catalog is `data/language-packs.json`. A package's
own manifest cannot grant itself permission. Local testing and distribution
are independent catalog permissions. Both current packages forbid distribution.
The catalog is provenance/integrity metadata, not a signature: users must obtain
it with a trusted application release. Supplying an attacker-owned catalog to a
build CLI defeats this trust assumption; do not expose that CLI to downloaded packs.

`tools/language_pack.py` assembles only explicitly selected files after validating
all bytes and preserving notices unchanged. It does not download or discover
languages and never overwrites an existing output directory. Verification compares
against the trusted record and rejects missing, modified or extra content.
Hashes establish identity, not legal permission or linguistic quality.

## Current sources and permissions

The exact LibreOffice snapshot and source file identities are pinned in the
catalog. Upstream source paths are `en/en_US.aff`, `en/en_US.dic`,
`ru_RU/ru_RU.aff` and `ru_RU/ru_RU.dic`.
Primary licenses and obligations: [review](notices/LANGUAGE-PACKS-REVIEW.md).
Full English and Russian notices must accompany every local copy and derivative;
the engine, dictionaries, generated stems and model are separate components.
Rasklad's MIT license applies to its own assembly code, not these dictionaries.

The mapping in `data/language-pack-inputs.json` describes where previously pinned
project-local inputs would be staged under each package-relative filename. It is
metadata only. Creating these raw packages or running real-dictionary tests must
occur as an explicitly authorized data operation. No old one-shot test permission
is silently reused. This change's tests use only project-authored fictional bytes.

For an authorized, already staged source directory:

```sh
python3 tools/language_pack.py --catalog data/language-packs.json \
  --id libreoffice-en-us-c0865a85 --source build/language-pack-stage/en \
  --output build/language-packs/en
python3 tools/language_pack.py --catalog data/language-packs.json \
  --id libreoffice-en-us-c0865a85 --verify build/language-packs/en
```

Do not put raw packages or real dictionaries in Git. The current output is a
local preparation artifact, not a released or runtime-registered language pack.
A subsequent authorized integration test must feed verified bytes to the engine,
not reopen arbitrary paths after checking them. No runtime auto-update is added.

## Adding a language

1. Select a locale and immutable source revision. Inspect that language's actual
   `.aff`/`.dic` provenance and full terms before acquiring dictionary bytes.
2. Record exact files, sizes and SHA-256; preserve full applicable notices and
   explicit transformation records. Review local and distribution operations
   separately. Unknown or conflicting rights block that operation.
3. Add a reviewed catalog entry; never infer permissions from a neighboring pack
   or the repository's main license. Test assembly and tamper rejection offline.
4. Establish engine support for encoding/flags, then separately add layout maps,
   canonical alphabet, detector/model and ambiguity/IME tests for the language pair.
   Verify real memory, accuracy and latency before enabling automatic correction.
5. Ship only the explicitly enabled/selected packs with their complete notices.
   Future installation/removal must be explicit and atomic; no background fetching.

The package contract accepts additional locales. The current correction core,
model and browser adapter remain EN/RU: adding a French dictionary, for example,
does not silently make the EN/RU classifier correct French. Additional languages
with complex scripts/IME or encodings need explicit capability work. This keeps
dictionary expansion possible without hiding unsupported correction behavior.

## Checks

```sh
python3 -m unittest discover -s tests/language_pack -v
```

These tests check provenance enforcement, byte-exact notices, independent locales,
resource limits, hostile paths/symlinks, tampering, extra content and no-overwrite
behavior, without opening actual dictionaries or user text.
