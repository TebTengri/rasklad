# rasklad

Fixes words typed in the wrong keyboard layout, as you type. For
[Omarchy](https://omarchy.org) and Hyprland, Russian and English.

`ghbdtn` + Space → `привет `. `руддщ` + Space → `hello `.

Rasklad is an add-on for [Fcitx5](https://fcitx-im.org) plus a settings window in
the Omarchy style. Everything runs locally: the dictionaries and the model ship in
the package, and nothing uses the network.

Status: in daily use by the project owner, but built from source only (an Arch
package from this repository). The Russian [README.md](README.md) is the primary
text; this is its translation.

## What it does

- **Automatically at the end of a word.** On Space, Enter, Tab or a punctuation
  key that ends the word, a word typed in the wrong layout is replaced. Keys that
  type a letter in the other layout (the comma key is Russian `б`) stay part of the
  word until the next separator. A word is replaced only when Hunspell and the
  model agree, or when it is on a fixed list of common words: `rfr` → `как`,
  `ns` → `ты`, `щл` → `ok`, `ghbdtn` → `привет`. Case is kept: `Ghbdtn` →
  `Привет`, `GHBDTN` → `ПРИВЕТ`. Mixed case, unknown and ambiguous words are left
  alone.
- **Swap the last word.** Right after the Space (or the punctuation that ended the
  word), a double Shift swaps the word just finished: after a replacement it
  brings back what you typed (another double Shift toggles back), and a word that
  was not replaced is put into the other layout (`hello ` → `руддщ `). It works
  only if you have pressed nothing since and the application reports the text
  before the cursor, which must end with that word and its separator. Otherwise it
  does nothing. It does not work after Enter or Tab, or more than 15 seconds after
  the word.
- **While typing.** The word being typed is underlined; a double Shift converts it
  to the other layout, and automatic correction then leaves that word alone.
- **Layout.** A double left Alt switches EN ↔ RU in any window.
- **Omarchy bar icon** shows EN/RU and whether auto-correction is on.
- **Exceptions.** In the settings window, the Exceptions tab lists words that
  auto-correction never touches, in either layout. The double Shift still works
  for them.

Auto-correction and the double Shift work only in the applications switched on in
the settings; by default Slack and Chromium. The double Alt and the icon work
everywhere.

## Keys

| Where | Keys | Action |
| --- | --- | --- |
| Any window | double left Alt | switch layout EN ↔ RU |
| Enabled application | double Shift right after Space (or a word-ending mark) | undo the replacement, or put the word into the other layout |
| Enabled application | double Shift on the underlined word | convert the word to the other layout |
| Bar icon | left click | switch layout |
| Bar icon | middle click | pause or resume auto-correction |
| Bar icon | right click | open the settings |

In the settings window:

| Keys | Action |
| --- | --- |
| Ctrl+1, Ctrl+2, Ctrl+3 | tabs General, Applications, Exceptions |
| Ctrl+Tab, Ctrl+Shift+Tab | next and previous tab |
| ↓/↑ or j/k | next and previous row |
| Space or Enter | toggle the row |
| Ctrl+N | add an application by hand |
| Ctrl+R or F5 | refresh the application list |
| F1 or ? | help |
| Esc | close the form or help, then the window |
| Q or Ctrl+Q | close the window |

A single Shift or Alt, Shift with a letter and Alt with another key work as
usual. Right Alt is not used: Omarchy starts dictation with it.

## Install

You need Arch Linux with Omarchy, Fcitx5, Qt 6 and Hunspell 1.7.

```sh
profile="/path/to/profile"                         # see PROFILE.md below
tools/build_arch_package.sh "$profile"             # package in build/arch-package/
sudo pacman -U build/arch-package/rasklad-fcitx5-*-x86_64.pkg.tar.zst
fcitx5 -rd                                         # restart Fcitx
rasklad-setup --install                            # connect it to the desktop
rasklad-settings                                   # turn Rasklad on
```

After installation Rasklad is off: the addon is loaded but does nothing — no
correction, no double Shift, no sound. The «Rasklad is on» switch in the settings
window turns it on at once, without restarting Fcitx, and off again.
`rasklad-setup` does not turn it on.

The detector profile (dictionaries and model) is not kept in Git. Where it comes
from and how to reproduce it from a clean clone:
[packaging/arch/PROFILE.md](packaging/arch/PROFILE.md).

The package does not change your settings by itself. `rasklad-setup --install`
shows the changes and asks first. It adds:
- the double Alt in Hyprland;
- the Rasklad icon in place of the standard layout icon;
- the EN and RU layouts in Fcitx.

In a separate question (default: no), it offers to keep only the `us` layout in
Hyprland, with no group switch (`grp:`), so that only Fcitx switches layouts. The
setting is appended to `~/.config/hypr/input.lua`; earlier lines stay. The lock
screen takes its layout from Hyprland, so its password can then be typed in Latin
letters only. If there are layouts other than `us` and `ru` (`kz`, for example), or
`us` has a non-Latin variant, the step is not offered. Non-interactive install:
`rasklad-setup --install --yes --single-layout`.

`rasklad-setup --revert` puts back what it changed. A file you changed yourself
after the last install is normally left as it is, with a notice. From the Fcitx
layout group it takes out only the layouts that its last saved record says it
added; the others, their order and the default layout stay as they are. Known gaps: an edit made in the instant between its check and the
replacement is not caught, and it removes a layout matching that record even if
you added it yourself. That happens if you removed its layout and added the same
one while the record never learned of the removal, or if a run recorded a layout
but never got confirmation that it was added. The record does not learn of a
change when no `--install` or
`--revert` ran in between (`--check` and the settings window's check record
nothing), when a run could not save its record (it stopped, Fcitx gave no answer,
the disk was full), and with a Fcitx group deleted and created again under the
same name. Only one `--install` or `--revert`
runs at a time for the same state directory. The same actions are in the settings window, section "Connection to
Omarchy"; keeping only `us` in Hyprland is offered in the terminal only
(`rasklad-setup --install`).

Details, manual setup and checks after installing:
[packaging/arch/INSTALL.md](packaging/arch/INSTALL.md) (Russian).

Uninstall (the package is removed only if the revert succeeded):

```sh
rasklad-setup --revert && sudo pacman -R rasklad-fcitx5 && fcitx5 -rd
```

If the package was removed without a revert, the double Alt and the single-layout
block in `input.lua` (written by 0.5.22 or later) stop applying at the next
`hyprctl reload`. Remove an older block from `input.lua` by hand. The bar
may keep an empty widget slot: put `omarchy.keyboard-layout` back into
`~/.config/omarchy/shell.json` (a copy is in
`~/.config/omarchy/shell.json.rasklad.bak`). `pacman -R` reminds you of this.
After `omarchy refresh`, run `rasklad-setup --install` again: if `bindings.lua` or
`shell.json` was reset, it forgets its records for them (if `input.lua` was reset too,
run `rasklad-setup --revert` first). A file with every mention of rasklad taken
out counts as your own configuration.

## Limits

- Only the `us` ↔ `ru` pair (standard variants). Phonetic, typewriter, Dvorak and
  other layouts are not supported.
- Terminals are not supported: the add-on refuses known terminals even when added
  by hand, and fields the application marks as a terminal. A terminal it does not
  know, such as a web terminal in a browser, it cannot tell apart.
- The add-on skips fields the application marks as password, hidden, terminal,
  e-mail, URL, number, phone, date or time. A field without such a mark, password
  fields included, looks like ordinary text to Rasklad. That is why it works only
  in the listed applications (Slack and Chromium by default; you can switch them
  off).
- If a web page moves the focus to another field in the same millisecond as you
  press Space, the corrected word can land in the new field
  ([docs/SYS-271-FOCUS-RACE.md](docs/SYS-271-FOCUS-RACE.md)).
- If the application sends no text around the cursor, or more than 64 KiB of it,
  the double Shift after a word does nothing.
- The double Alt is not swallowed: the application sees both Alt presses too (a
  menu bar may flash in Slack and other Electron apps).
- If Fcitx crashes, the word being typed is lost.
- The add-on is built against Hunspell 1.7. If an update removes
  `libhunspell-1.7.so.0`, Fcitx does not load the add-on at all. A rebuild against
  Hunspell 1.8 brings back the double Shift but not auto-correction, which works
  with Hunspell 1.7 only. If the add-on loads but the profile fails its check,
  auto-correction is off, the icon shows `EN!`/`RU!`, and the double Shift stays.

## Privacy

- Rasklad never sends typed text to the network or writes it to logs or files.
  (Like any program's memory, Fcitx's may still reach disk through swap or a core
  dump.)
- Rasklad keeps nothing while it is off. When on, it keeps in memory, per field,
  only the word being typed and the last finished word. The last word is dropped at the next key other than Shift, or at
  a focus change; after
  15 seconds it can no longer be swapped, and its stored copy is then wiped
  (temporary copies made while correcting may linger in freed memory).
- The settings window stores only the application list, the exceptions and the
  switches, in `~/.config/fcitx5/conf/rasklad.conf`.
- The names of programs where you placed the cursor, without any text, are kept
  in Fcitx's memory while it runs and Rasklad is on (cleared when it is turned
  off), and are readable by your own programs over the
  session bus (the settings window shows them).

Security boundaries and everything the add-on and the window run:
[SECURITY.md](SECURITY.md) (Russian).

## Development

- Stack: C++20, Fcitx5, Qt 6 Quick, qmake6.
- Add-on: `src/raskladproduct.cpp`; shared rules in `src/configrules.h` and
  `src/wordrules.h`.
- Settings window: `settings/`.
- Package tests (`check()` in `packaging/arch/PKGBUILD`): policy, permissions,
  sound, word and configuration rules, the gesture, the settings window.
- Isolated Wayland lab on the built package (unpacked into
  `build/arch-package/x/`):

  ```sh
  mkdir -p build/arch-package/x
  bsdtar -xf build/arch-package/rasklad-fcitx5-*-x86_64.pkg.tar.zst -C build/arch-package/x
  python3 tools/wayland_ime_lab.py --run --product-suite listed --packaged --input-root "$profile"
  ```

  Suites: `listed`, `unlisted`, `layout`, `exceptions`, `gestures`, `focus`.
- Rules for people and agents: [AGENTS.md](AGENTS.md) (Russian).

## License

Rasklad's own code and documentation are under [MIT](LICENSE). Third-party
libraries, dictionaries, models and files with their own notices keep their terms:
see [THIRD_PARTY.md](THIRD_PARTY.md) and the [data policy](docs/DATA_POLICY.md).
The project's MIT grants no new right to redistribute that data or a built binary
package.

Dictionaries:
- English: [LibreOffice/dictionaries `c0865a85eb67`, en](https://github.com/LibreOffice/dictionaries/tree/c0865a85eb67dc377f2cf8e8fe2c4fa9e53f0dcb/en),
  built from [SCOWL](http://wordlist.aspell.net/) 2020.12.07 (Kevin Atkinson et al.;
  it uses UKACD, Copyright (c) J Ross Beresford 1993-2000);
  [terms](data/notices/en_US.README.txt), [UKACD document](data/notices/UKACD-readme.txt);
- Russian: [LibreOffice/dictionaries `c0865a85eb67`, ru_RU](https://github.com/LibreOffice/dictionaries/tree/c0865a85eb67dc377f2cf8e8fe2c4fa9e53f0dcb/ru_RU),
  Copyright (c) 1997-2008 Alexander I. Lebedev; [terms](data/notices/ru_RU.README.txt).

Rasklad changed part of this data and built a model from it; the changes are
listed in [DATA-NOTICE.txt](packaging/arch/DATA-NOTICE.txt). The dictionary authors have not
endorsed and do not support Rasklad.

The package puts the full dictionary notices, the UKACD document and the
description of Rasklad's changes in `/usr/share/licenses/rasklad-fcitx5/`. The same
texts open in the settings window: Help (F1) → Licenses. Distribution review by
exact hashes: [DISTRIBUTION-REVIEW-001](data/notices/DISTRIBUTION-REVIEW-001.md).
A public release of the
package with this data awaits the owner's decision.
