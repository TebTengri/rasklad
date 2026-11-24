# Distribution review 001: the profile in rasklad-fcitx5 (SYS-256)

Prepared by Claude, 2026-11-24. Independent review: Codex (see the PR). This is a
technical check of the reviewed notices against the exact package contents. It is
not legal advice and not a guarantee of uncontested rights.

**Decision: pending the owner.** `release_approved` stays `false` everywhere
(passports, `MODIFICATIONS.txt`, the check in `src/profileverify.cpp`) until the
owner decides. Flipping it is one separate change: passports, the profile's
`MODIFICATIONS.txt`, the verifier and its pins together.

## Exact contents

Package 0.5.17, `/usr/share/fcitx5/rasklad/profile/` (SHA-256, bytes). The
hashes are of the profile the owner's packages are built from; any rebuilt
profile must be compared again.

| File | SHA-256 | Bytes | Status |
| --- | --- | --- | --- |
| build/experiment-data/en.dic | f0b1a234bd178bdd01875b2a392a9647f888b8fe879f79c52aae62c2759b3647 | 551762 | upstream, unmodified |
| build/experiment-data/ru.dic | f6047416a0204adbecf3a451b874ec8a97ee37e2cbc714466ef04d8dbcc0d6fc | 3473191 | upstream, unmodified |
| build/experiment-003-data/en.aff | e746c882dd6f303c2c46e7452804b9201115a6942cfeb15f18f8edf774d2e24e | 3205 | upstream, unmodified |
| build/experiment-003-data/ru.aff | 38ce7d4af78e211e9bafe4bf7e3d6a2c420591136cb738ec6648f8fdf6524cd7 | 71236 | upstream, unmodified |
| build/experiment-003-data/control/en.aff | 60fde13d7b4856a2dcb4a70eb9320a7288e38f3cd11ca6a40509a1d22e81f08f | 1460 | **modified**: PFX/SFX removed |
| build/experiment-003-data/control/ru.aff | 097549718b2cc53e0c597abb01a467fb8161ac9b3cac35287107717e6e71dd19 | 162 | **modified**: PFX/SFX removed |
| build/experiment-003-data/control/MODIFIED.txt | — | — | Rasklad marker, a copy of `packaging/arch/DATA-NOTICE.txt`; not part of the verified profile |
| build/experiment-002-artifacts/detector-model.rskn | e92e549e5f0853e4cd78d6d997653098b78550eb9911de11421434e26e3985d8 | 88290 | **derived**: model weights |
| build/experiment-002-artifacts/exact-full-lexicons.rskp | 3bdd740d4b39bf2b5fe361e0d3c670187563ec59f41913db3525340a30887871 | 1200379 | **derived**: word list |
| build/experiment-002-artifacts/artifact-metadata.json | 9a55205497c9cea58a157b83a3eeca3d66be899679265888443d1f18fee79612 | 1827 | Rasklad metadata |
| build/experiment-002-artifacts/MODIFICATIONS.txt | e2a4d9b6956c3210d0f60cf65432c7f8bfe3b891852f39832e46e4e6b3408af5 | 182 | Rasklad record |
| build/experiment-002-artifacts/en_US.NOTICE.txt | 168b4c01cb841f766a72f310b065c04d09cff46376c37a81d799593ce751c371 | 15732 | upstream notice, unmodified |
| build/experiment-002-artifacts/ru_RU.NOTICE.txt | 262af2f6ad70a61e5ee1332ff44fa8ee50edca819cf33207d8ad6ba6a0c9be52 | 1886 | upstream notice, unmodified |

What the addon enforces at load time (`src/profileverify.cpp`): size and SHA-256 of
the dictionaries, `.aff`, control `.aff`, model and word list; the notice copies by
SHA-256; `artifact-metadata.json` by its fields and `MODIFICATIONS.txt` by content
(UTF-8, the `release_approved=false` marker). The hashes of those two files above
are recorded here, not enforced by the addon.

The notice hashes equal `data/notices/en_US.README.txt` and `ru_RU.README.txt`,
which [LANGUAGE-PACKS-REVIEW](LANGUAGE-PACKS-REVIEW.md) matched against upstream.
The package also carries `data/` passports and notices under `profile/data/`.

## Obligations and how the package meets them

"Shipped" means what the package does. "Conclusion" is this review's reading, for
the owner to accept or not.

| Obligation (source) | Shipped | Conclusion |
| --- | --- | --- |
| Keep copyright, conditions and disclaimer in source and binary distributions, or in the documentation that comes with them (ru_RU; SCOWL, Ispell, VarCon and others in en_US) | Full notices in `/usr/share/licenses/rasklad-fcitx5/en_US.README.txt`, `ru_RU.README.txt` and in the profile directory; shown on the settings page «Лицензии» | Met for the package, unmodified and derived files alike |
| WordNet: the copyright notice, statements and disclaimer appear on **all copies** of the database and documentation, including modifications (en_US) | The full en_US notice, which contains the WordNet text, ships in the same package and in the same profile directory as every file derived from it | Met for the package as distributed: every copy of the data comes with the notice. The binary model and word list carry no notice inside the file, and a file taken out of the package would not carry it. Whether "accompanies every copy" reads as "appears on all copies" is **open**; the alternative is a notice embedded in the files, which needs a format change, new pins and a rebuilt profile |
| Modified versions clearly marked (ru_RU) and binary redistributions of modified source marked as modified in the documentation (Ispell clause 3, en_US) | `DATA-NOTICE.txt` in the licenses directory names each modified and derived file; the same text sits next to the control `.aff` as `MODIFIED.txt`; the «Что изменил Rasklad» page | Met in the documentation, which is what Ispell clause 3 asks for binaries. For ru_RU, whether a marker next to the file is enough or each control `.aff` needs its own header line is **open** (see below) |
| No endorsement or promotion with an author's name (ru_RU: Lebedev; Ispell: Kuenning) | No author is named in descriptions or promotion; `DATA-NOTICE.txt` says so | Met, as long as release texts keep it so |
| No use of Princeton's name in advertising or publicity about distribution (WordNet) | Princeton appears only inside the full notice | Met, as long as release texts keep it so; a separate rule from non-endorsement |
| UKACD copyright displayed prominently (en_US) | UKACD line on the «Лицензии» page itself and at the top of `DATA-NOTICE.txt` | Met |
| Derived data keeps the source terms (LANGUAGE-PACKS-REVIEW) | `DATA-NOTICE.txt` lists the model and word list as derived; `license=` names MIT and `LicenseRef-hunspell-en-US`, `LicenseRef-hunspell-ru-RU` | Met for the record. The model weights and the word list are derivatives of both dictionaries; every term above applies to them, and the permissive terms allow distributing modified and derived forms with notices |
| Project MIT does not cover the data | LICENSE, README, THIRD_PARTY.md, `DATA-NOTICE.txt` | Met |

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
2. If yes: whether the WordNet notice accompanying the files is enough, or the
   model and word list must embed it.
3. Whether the adjacent marker is enough for the control `.aff`, or each
   gets a header line (new pins, rebuilt profile).
4. Then one change flips `release_approved` with the verifier and pins.
