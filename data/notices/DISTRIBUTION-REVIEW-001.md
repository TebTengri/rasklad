# Distribution review 001: the profile in rasklad-fcitx5 (SYS-256)

Prepared by Claude, 2026-11-24. Independent review: Codex (see the PR). This is a
technical check of the reviewed notices against the exact package contents. It is
not legal advice and not a guarantee of uncontested rights.

**Decision: pending the owner.** `release_approved` stays `false` everywhere
(passports, `MODIFICATIONS.txt`, the check in `src/profileverify.cpp`) until the
owner decides. Flipping it is one separate change: passports, the profile's
`MODIFICATIONS.txt`, the verifier and its pins together.

## Exact contents

Package 0.5.16, `/usr/share/fcitx5/rasklad/profile/` (SHA-256, bytes):

| File | SHA-256 | Bytes | Status |
| --- | --- | --- | --- |
| build/experiment-data/en.dic | f0b1a234bd178bdd01875b2a392a9647f888b8fe879f79c52aae62c2759b3647 | 551762 | upstream, unmodified |
| build/experiment-data/ru.dic | f6047416a0204adbecf3a451b874ec8a97ee37e2cbc714466ef04d8dbcc0d6fc | 3473191 | upstream, unmodified |
| build/experiment-003-data/en.aff | e746c882dd6f303c2c46e7452804b9201115a6942cfeb15f18f8edf774d2e24e | 3205 | upstream, unmodified |
| build/experiment-003-data/ru.aff | 38ce7d4af78e211e9bafe4bf7e3d6a2c420591136cb738ec6648f8fdf6524cd7 | 71236 | upstream, unmodified |
| build/experiment-003-data/control/en.aff | 60fde13d7b4856a2dcb4a70eb9320a7288e38f3cd11ca6a40509a1d22e81f08f | 1460 | **modified**: PFX/SFX removed |
| build/experiment-003-data/control/ru.aff | 097549718b2cc53e0c597abb01a467fb8161ac9b3cac35287107717e6e71dd19 | 162 | **modified**: PFX/SFX removed |
| build/experiment-002-artifacts/detector-model.rskn | e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8 | 88290 | **derived**: model weights |
| build/experiment-002-artifacts/exact-full-lexicons.rskp | 3bdd740d4b39bf2b5fe361e0d3c670187563ec59f41913db3525340a30887871 | 1200379 | **derived**: word list |
| build/experiment-002-artifacts/artifact-metadata.json | 9a55205497c9cea58a157b83a3eeca3d66be899679265888443d1f18fee79612 | 1827 | Rasklad metadata |
| build/experiment-002-artifacts/MODIFICATIONS.txt | e2a4d9b6956c3210d0f60cf65432c7f8bfe3b891852f39832e46e4e6b3408af5 | 182 | Rasklad record |
| build/experiment-002-artifacts/en_US.NOTICE.txt | 168b4c01cb841f766a72f310b065c04d09cff46376c37a81d799593ce751c371 | 15732 | upstream notice, unmodified |
| build/experiment-002-artifacts/ru_RU.NOTICE.txt | 262af2f6ad70a61e5ee1332ff44fa8ee50edca819cf33207d8ad6ba6a0c9be52 | 1886 | upstream notice, unmodified |

The notice hashes equal `data/notices/en_US.README.txt` and `ru_RU.README.txt`,
which [LANGUAGE-PACKS-REVIEW](LANGUAGE-PACKS-REVIEW.md) matched against upstream.
The package also carries `data/` passports and notices under `profile/data/`.

## Obligations and how the package meets them

| Obligation (source) | Where it is met |
| --- | --- |
| Keep the copyright, conditions and disclaimer in source and binary distributions (ru_RU; SCOWL/Ispell/WordNet/VarCon terms in en_US) | Full notices in `/usr/share/licenses/rasklad-fcitx5/en_US.README.txt`, `ru_RU.README.txt`, also in the profile directory; shown in the settings window, «Лицензии» |
| Modified versions clearly marked (ru_RU) | `DATA-NOTICE.txt` in the licenses directory and as `control/MODIFIED.txt` next to the modified `.aff`; the «Что изменил Rasklad» page; `MODIFICATIONS.txt` in the profile |
| No use of an author's name to endorse (ru_RU, WordNet, en_US terms) | No author is named in descriptions or promotion; `DATA-NOTICE.txt` says so |
| UKACD copyright displayed prominently (en_US README) | UKACD line on the «Лицензии» page itself and at the top of `DATA-NOTICE.txt` |
| Derived data keeps the source terms (LANGUAGE-PACKS-REVIEW) | `DATA-NOTICE.txt` lists the model and word list as derived; `license=` in PKGBUILD names both dictionary licenses next to MIT |
| Project MIT does not cover the data | LICENSE, README, THIRD_PARTY.md, `DATA-NOTICE.txt` |

## Not covered by this review

- Qt, Fcitx 5 and Hunspell are not shipped: the package depends on the Arch
  packages and links dynamically. Their notices come with those packages.
- The modified control `.aff` files are not changed byte-wise to add a header: that
  would change the pinned hashes. They are marked by the adjacent `MODIFIED.txt`
  and `DATA-NOTICE.txt`. Whether an adjacent marker is enough is part of the
  owner's decision; the alternative is a header line in each control `.aff` plus
  new pins and a rebuilt profile.
- Personal names and census provenance in the en_US word lists (see the README):
  the package is not advertised as anonymized data.
- Additional languages: each needs its own review.

## What the owner decides

1. Whether the package may be published (GitHub release, AUR) with this data, or
   stays for personal installation.
2. If yes: whether the adjacent marker is enough for the control `.aff`.
3. Then one change flips `release_approved` with the verifier and pins.
