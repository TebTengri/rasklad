# RU/EN dictionary package review

Reviewer: Codex, 2026-11-13. Scope: the exact LibreOffice/dictionaries snapshot
`c0865a85eb67dc377f2cf8e8fe2c4fa9e53f0dcb`, not every dictionary in that repository.
This records a technical license review, not a guarantee of uncontested rights.
No dictionary bytes or trained weights were opened by this review.

## Russian

Primary source: https://raw.githubusercontent.com/LibreOffice/dictionaries/c0865a85eb67dc377f2cf8e8fe2c4fa9e53f0dcb/ru_RU/README_ru_RU.txt
The full notice is retained unchanged as ru_RU.README.txt. Its current upstream
bytes were independently fetched and matched SHA-256
262af2f6ad70a61e5ee1332ff44fa8ee50edca819cf33207d8ad6ba6a0c9be52.
Alexander I. Lebedev's notice permits use and source/binary redistribution,
with or without modification, provided notices/disclaimer are retained,
modifications are marked, and his name is not used for endorsement without permission.
The upstream notice also records Laszlo Nemeth's affix modification.
This is a custom permissive notice, not the project's MIT license.

## American English

Primary source: https://raw.githubusercontent.com/LibreOffice/dictionaries/c0865a85eb67dc377f2cf8e8fe2c4fa9e53f0dcb/en/README_en_US.txt
The full 2020.12.07 README is retained unchanged as en_US.README.txt, with SCOWL,
WordNet, VarCon, Ispell and other embedded credits/terms. It explicitly covers
both the derived dictionaries and the Ispell-derived affix file. We retain the
ENTIRE notice rather than infer that a size-60 wordlist exempts any component.
Applicable permissions require preservation of the respective copyright,
permission and disclaimer text, marking modifications, and respecting naming/
endorsement conditions. The UKACD notice requests prominent copyright display;
accent information is attributed to UKACD, so the full text and that copyright
must remain prominent in the package's notices. Public-domain statements are
retained as upstream provenance, not replaced by an unsupported MIT label.
README statements about personal names/census provenance do not establish a
new independent warranty; do not advertise this package as anonymized data.

## Allowed preparation and remaining release gate

Local packaging of the pinned raw .aff/.dic, integrity checking and isolated
spell tests are technically admitted by these reviewed permissions, provided
complete notices and a MODIFICATIONS record accompany every local package.
Raw files are copied byte-for-byte: no stripping flags, lowercasing, frequency
claims or hidden conversion. The full/control PFX/SFX removal is a separate
in-memory engine transformation, already marked in the spell adapter docs.
Its provenance remains subject to the same notices; nothing becomes MIT just
because it was compressed, filtered or used to train weights.

The catalog's distribution flag REMAINS FALSE. Before a distributable package:
verify the actual complete package and notice hashes; present notices and the
UKACD copyright prominently in the shipped licensing view; retain and mark every
derivative; inspect all dictionary, model and engine/runtime source obligations
separately; record a new reviewer decision. No model/RSKF derivative is approved
by this raw-dictionary review. No binary engine is included in a language pack.

This review is not permission to inspect user text, install dictionaries into
system directories, or open additional local artifacts outside an authorized
operation. Tests during development use wholly project-authored fictional data.

## Additional languages

Each language/locale needs its own upstream source, immutable revision, hashes,
full applicable license/attribution files and operation-specific review. No
repository-wide or language-wide license inheritance. A common pack format
allows additional Hunspell UTF-8 dictionaries without executable plugins or
network discovery. Dictionary availability alone does not enable detection:
layout conversion, canonical alphabet, model, ambiguity policy, script/IME and
quality tests must separately support the new language/pair. The current router
remains EN/RU only. Unsupported pairs leave input unchanged.
