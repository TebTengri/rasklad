# Distribution review 001: the profile in rasklad-fcitx5 (SYS-256)

Prepared by Claude, 2026-11-24. Independent review: Codex (see the PR). This is a
technical check of the reviewed notices against the exact package contents. It is
not legal advice and not a guarantee of uncontested rights.

**Decision: approved by the owner on 2026-11-26.** The shipped passports
(`data/sources.json`, `data/product-profile-sources.json`) say
`release_approved: true` with `distribution_review` naming this file; the release
profile's `MODIFICATIONS.txt` and metadata scope state the approval; the addon
(`src/profileverify.cpp`) requires all of it. The research passports of experiments
001–006 keep `release_approved: false`: they record local experiments. Approval
covers the files below at these hashes; a changed profile needs this review again.

## Exact contents

Package 0.5.19 (checked again against 0.5.31 on 2026-11-26: every hash below
matches), `/usr/share/fcitx5/rasklad/profile/` (SHA-256, bytes), built by
`tools/make_release_profile.py` from the reviewed experiment profile. The
hashes are of the profile the owner's packages are built from; any rebuilt
profile must be compared again.

| File | SHA-256 | Bytes | Status |
| --- | --- | --- | --- |
| build/experiment-data/en.dic | f0b1a234bd178bdd01875b2a392a9647f888b8fe879f79c52aae62c2759b3647 | 551762 | upstream, unmodified |
| build/experiment-data/ru.dic | f6047416a0204adbecf3a451b874ec8a97ee37e2cbc714466ef04d8dbcc0d6fc | 3473191 | upstream, unmodified |
| build/experiment-003-data/en.aff | e746c882dd6f303c2c46e7452804b9201115a6942cfeb15f18f8edf774d2e24e | 3205 | upstream, unmodified |
| build/experiment-003-data/ru.aff | 38ce7d4af78e211e9bafe4bf7e3d6a2c420591136cb738ec6648f8fdf6524cd7 | 71236 | upstream, unmodified |
| build/experiment-003-data/control/en.aff | 4c2ee7fbf1aa4a8ea31503b7a7c4688d4a84d99bcbd9f4a0783842ec333e1b19 | 1618 | **modified**: PFX/SFX removed; marked by a header comment |
| build/experiment-003-data/control/ru.aff | 2869341971555f4f585e739e911dd4c921f4b636b955c5b06210bb443b49f438 | 320 | **modified**: PFX/SFX removed; marked by a header comment |
| build/experiment-003-data/control/MODIFIED.txt | — | — | Rasklad marker, a copy of `packaging/arch/DATA-NOTICE.txt`; not part of the verified profile |
| build/experiment-002-artifacts/detector-model.rskn | d30f471a13be898514b030fbab95872b69d431beb8e105bc56b860a3c163509a | 106396 | **derived**: model weights; full notices inside (RSKX) |
| build/experiment-002-artifacts/exact-full-lexicons.rskp | ff325623b4dd2fe871195138d7b09c1c5dd0384152bca85e8c9f66789dd15da3 | 1218480 | **derived**: word list; full notices inside (RSKX) |
| build/experiment-002-artifacts/artifact-metadata.json | 4b0b0994e25cf8f0a325fa30e95f9fd849517dd29de7625d195b61e98c714e8c | 1917 | Rasklad metadata (0.5.32: states the approval) |
| build/experiment-002-artifacts/MODIFICATIONS.txt | ae36f26833e66a6902accc2e7128ef0ad3102046613c0e9820657fb6a3398f46 | 381 | Rasklad record (0.5.32: states the approval) |
| build/experiment-002-artifacts/en_US.NOTICE.txt | 168b4c01cb841f766a72f310b065c04d09cff46376c37a81d799593ce751c371 | 15732 | upstream notice, unmodified |
| build/experiment-002-artifacts/ru_RU.NOTICE.txt | 262af2f6ad70a61e5ee1332ff44fa8ee50edca819cf33207d8ad6ba6a0c9be52 | 1886 | upstream notice, unmodified |

What the addon enforces at load time (`src/profileverify.cpp`): size and SHA-256 of
the dictionaries, `.aff`, control `.aff`, model and word list; the notice copies by
SHA-256; `artifact-metadata.json` by its fields and `MODIFICATIONS.txt` by content
(UTF-8, the `release_approved=true` statement). The hashes of those two files above
are recorded here, not enforced by the addon.

The notice hashes equal `data/notices/en_US.README.txt` and `ru_RU.README.txt`,
which [LANGUAGE-PACKS-REVIEW](LANGUAGE-PACKS-REVIEW.md) matched against upstream.
The package also carries `data/` passports and notices under `profile/data/`.

## Obligations and how the package meets them

"Shipped" means what the package does. "Conclusion" is this review's reading, for
the owner to accept or not.

| Obligation (source) | Shipped | Conclusion |
| --- | --- | --- |
| Keep copyright, conditions and disclaimer with source and binary distributions and in their documentation (ru_RU; Ispell; the supporting-documentation part of SCOWL, VarCon and WordNet) | Full notices in `/usr/share/licenses/rasklad-fcitx5/en_US.README.txt`, `ru_RU.README.txt` and in the profile directory; shown on the settings page «Лицензии» | Met for the package |
| The copyright notice in **all copies**, including modifications (SCOWL and VarCon: "appears in all copies"; WordNet: notice, statements and disclaimer "on ALL copies ... including modifications") | The model and the word list begin with the full en_US and ru_RU notices inside the file (RSKX container); the verifier refuses a file without it. Every other file ships next to the full notices | Met: a copy of the derived file carries the notices even when taken out of the package |
| Modified versions clearly marked (ru_RU) and binary redistributions of modified source marked as modified in the documentation (Ispell clause 3, en_US) | Each control `.aff` starts with a comment: modified by Rasklad, PFX/SFX removed, not an upstream file; the verifier requires it. Also `DATA-NOTICE.txt`, `control/MODIFIED.txt` and the «Что изменил Rasklad» page | Met in the file itself and in the documentation |
| No endorsement or promotion with an author's name (ru_RU: Lebedev; Ispell: Kuenning) | No author is named in descriptions or promotion; `DATA-NOTICE.txt` says so | Met, as long as release texts keep it so |
| No use of Princeton's name in advertising or publicity about distribution (WordNet) | Princeton appears only inside the full notice | Met, as long as release texts keep it so; a separate rule from non-endorsement |
| UKACD copyright displayed prominently (en_US) | UKACD line (1993-2000, from its own document) on the «Лицензии» page itself and at the top of `DATA-NOTICE.txt` | Met |
| UKACD: "the text of this document must be included verbatim" if UKACD is used in a software package (en_US) | The UKACD document itself, version 1.7, byte for byte from the SCOWL 2020.12.07 source archive (`data/notices/UKACD-readme.txt`, passport `data/ukacd-source.json`), installed as `/usr/share/licenses/rasklad-fcitx5/UKACD-readme.txt` and shown on the «Лицензии» page | Met. Shipped whether or not UKACD words reach the size-60 list, so the question of applicability no longer matters |
| Derived data keeps the source terms (LANGUAGE-PACKS-REVIEW) | `DATA-NOTICE.txt` lists the model and word list as derived; `license=` names MIT and `LicenseRef-hunspell-en-US`, `LicenseRef-hunspell-ru-RU` | Met for the record. The model weights and the word list are derivatives of both dictionaries; every term above applies to them, and the permissive terms allow distributing modified and derived forms with notices |
| Project MIT does not cover the data | LICENSE, README, THIRD_PARTY.md, `DATA-NOTICE.txt` | Met |

## Not covered by this review

- Qt, Fcitx 5 and Hunspell are not shipped: the package depends on the Arch
  packages and links dynamically. Their notices come with those packages.
- Personal names and census provenance in the en_US word lists (see the README):
  the package is not advertised as anonymized data.
- Additional languages: each needs its own review.

## The owner's decision

2026-11-26: approved. The change that records it (0.5.32) flips `release_approved`
in the shipped passports, `MODIFICATIONS.txt`, the metadata scope and the verifier
together, and is checked like any release. Publishing itself is a separate decision
of the owner.
