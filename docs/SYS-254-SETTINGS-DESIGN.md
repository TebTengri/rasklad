# SYS-254: the Rasklad settings window, Omarchy style

This design merges three independent studies from 2026-11-24:
- a study of the Omarchy design language (`/usr/share/omarchy/default/agents/skills/omarchy-app/SKILL.md`, `shell/Commons/{Style,Color}.qml`, `shell/Ui/{Toggle,ToggleSwitch,PanelSectionHeader,PanelKeyCatcher}.qml`);
- a UX and copy review;
- an independent proposal from Codex.

The owner's goals:
- the window fits Omarchy;
- it is light and readable and covers every setting;
- no unneeded text;
- human app names;
- no ad-hoc wording, because the window serves many Russian-speaking users.

## Structure

The window is one scrolling page with no tabs, because Omarchy uses none. From top to bottom:

1. **Header.** «Rasklad» (display, 24) and one line in the secondary color: «Исправляет слова, набранные не в той раскладке: ghbdtn → привет.» A flat `?` button sits on the right.
2. **Banner.** It shows only when something is wrong. Only the most serious problem shows, in this order: Fcitx or the addon is down, the addon is older than the settings, the layouts are missing, the profile is broken. It sits inside a red bordered box and gives the action in one sentence. The permanent «Работает» status lines are gone.
3. **ИСПРАВЛЕНИЕ.** Two toggle rows:
   - «Автоисправление», with the detail line «Если выключено, слово исправляется двойным Shift.»
   - «Звук при смене раскладки», with no detail line.
4. **ПРИЛОЖЕНИЯ.**
   - The detail line under the header reads «Rasklad работает только во включённых приложениях. Терминалы не поддерживаются.» The old claim «поля паролей не трогаются никогда» is dropped, because an unmarked password field cannot be detected (see SECURITY.md).
   - Groups, each hidden when empty:
     - «Включено»: switched-on apps.
     - «Недавно использованные»: names from `SeenApplications`, plus names switched off in this session.
     - «Рекомендуемые»: developer-tested apps that are neither on nor seen.
   - Each row is one toggle row with an icon, the human name and a detail line.
     - The detail line is the description from the `.desktop` entry; tested apps carry no mark.
     - The raw program name appears only in the tooltip.
   - Rows are grouped at refresh and do not move when switched, so nothing jumps under the cursor.
   - A search field appears when there are more than 12 apps.
   - Below the groups:
     - the button «Добавить вручную…», which opens a program-name field and «Добавить»;
     - the hint «Нет в списке? Щёлкните по полю ввода в нужном приложении.»
5. **ИСКЛЮЧЕНИЯ.** The detail line reads «Эти слова не исправляются автоматически — ни в одной раскладке.» Below it come the word chips, then the «Слово» field and «Добавить».

These are removed:
- the tabs;
- the per-app badges «проверено», «не проверено», «замечено» and «терминал»;
- the app and word counters;
- the shortcut footer;
- the «Обновить» button (the list refreshes on focus; Ctrl+R and F5 still work);
- the double-Shift sentence in the header, which moves to help.

## Help (`?` or F1)

Help is an overlay inside the window. It contains:
- two lines on gestures:
  - «Двойной Shift меняет раскладку слова или отменяет только что сделанное исправление.»
  - «Двойной Alt переключает раскладку.»
- one line on password fields: an unmarked password field looks like ordinary text;
- one line on discovery: «Приложение появляется в списке, когда вы щёлкаете по полю ввода в нём. После перезапуска Fcitx список собирается заново.»
- the keys.

Esc or `?` closes it.

## Keys

| Key | Action |
|---|---|
| `?`, F1 | Help |
| `Q` (Й), Ctrl+Q | Quit, when no text field is focused |
| Esc | Close help or the add field; in a field, clear it, then leave it; otherwise quit |
| Tab, Shift+Tab | Next or previous control |
| `j`/`k` (о/л), ↓/↑ | Next or previous row |
| Space, Enter | Toggle the focused row |
| Ctrl+N | Add manually |
| Ctrl+R, F5 | Refresh |

## Look: Omarchy tokens

- **Font:** `monospace`, which is JetBrainsMono Nerd Font on Omarchy.
- **Sizes:** caption 10, bodySmall 11, body 12, subtitle 13, title 14, heading 16, display 24.
- **Colors:** from the theme's `colors.toml`.
  - Main roles: `background`, `foreground`, `accent`.
  - Errors use `red`, falling back to `color1`.
  - Secondary text is `darker(foreground, 1.5)`, because the theme's `muted` is too low-contrast for text.
  - Section headers are 10 px bold in `darker(foreground, 1.4)`, below a 1 px separator in foreground at 0.12 alpha.
- **Rows and controls** are the foreground at these alphas:
  - normal: fill 0.04, 1 px border at 0.4;
  - hover or keyboard cursor: fill 0.08, border 0.25;
  - selected or on: fill 0.18;
  - pressed: fill 0.22.
- **Toggle rows** are at least 54 px tall. The whole row takes the click, and the switch inside only shows state.
- **The switch** is square with square corners and a pill only when rounded. When on, the track is foreground at 0.18 and the knob is the foreground.
- **Radius** comes from Hyprland's `decoration:rounding`; small parts use `min(4, r)`.
- **Accent** marks only the keyboard focus ring and the selected row bar. It is never used as a badge color.
- **Material** is always Dark on a dark palette.

## App names: `.desktop` index

The index lives in `settings/desktopindex.{h,cpp}`. It is a small parser: it reads only the `[Desktop Entry]` group and only the keys Type, Name*, GenericName*, Comment*, Icon, StartupWMClass, Exec, NoDisplay and Hidden.

**Where it looks:** the XDG application directories, with user directories first.

**How it matches a program name**, case-insensitive, first hit wins:
1. `StartupWMClass`;
2. the desktop file id, or its last reverse-DNS segment;
3. a unique `Exec` basename.

**What it shows:**
- Name: `Name[ru_RU]`, then `Name[ru]`, then `Name`.
- Description: `Comment[ru]`, then `GenericName[ru]`, then `Comment`, then `GenericName`. It is hidden when it equals the name.
- Icon: `QIcon::fromTheme`, or a letter avatar when there is none.

**Special cases:**
- **Web apps.** `chrome-<site>__-Default` with no entry shows the site as the name and «Веб-приложение» as the description. It stays a separate permission from Chromium.
- **Aliases.** `chromium` and `chromium-browser` share one row. The row is on if either is on. A click writes both. A mixed state shows as on with the detail line «Включено не для всех имён программы» until the user clicks.
- **System components** are hidden: `quickshell`, `org.quickshell`, `walker`, `elephant`, `mako`, `waybar`, `swayosd`, `hyprlauncher`, `org.omarchy.screensaver` and `rasklad-settings`. Terminals stay hidden, as before.

**Privacy:**
- Of `Exec`, only the basename is kept.
- Nothing is logged or written.
- No network is used.

## Out of scope here, documented later

- An opt-in Omarchy menu entry (Setup → Typing → Rasklad), via the user's own `~/.config/omarchy/extensions/omarchy-menu.jsonc`. The package never writes user configuration.
- The floating window rule.

## Critique applied (Gemini, Grok)

- Removed «Совместимость проверена»: tested apps work quietly.
- Rows no longer jump between groups on a click.
- The partial alias state is said in words, not hidden.
- `j`, `k`, Space and Enter act only on a focused row; `Q` and `?` are ignored while a text field has focus.
- A search field for long lists.
- Not adopted: an "everywhere except" model. Rasklad stays opt-in per app on purpose (SECURITY.md: fields an app does not mark as passwords cannot be told apart).
- Accessibility review: focus returns to the same row after a switch or a refresh; the window opens with focus on the first row; a held Space switches once; help has a Close button, scrolls, and returns focus; rows expose checkbox role, state and actions.
- Codex review: an enabled system component stays listed so it can be switched off; names come from any alias; exact id breaks window-class ties; one deadline for the Hyprland exchange; held Esc/Q act once; the 64-app limit shows outside the add form.
- SYS-260: help says how to pause (the Automatic correction switch, or a middle click on the layout icon); no separate pause switch, because it would duplicate that setting.

## SYS-261: tabs after the owner's first week

- **Three tabs:** «Основные» (correction and the Omarchy connection), «Приложения» and «Исключения».
  - The owner chose this layout. With many apps installed, one page grew too long to reach the other settings.
  - The tabs carry Omarchy's look: a bold label and an accent underline on the current tab.
  - They switch with Ctrl+1/2/3, Ctrl+Tab, or ←/→ on a focused tab.
- **The Omarchy connection** collapses to one line when everything is connected. The rows and «Подключить…» appear only when something is missing.
- **Long titles wrap** instead of being cut.
- **One window.** Clicking the icon again brings the open window forward instead of opening a second one.
