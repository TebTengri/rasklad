-- SPDX-License-Identifier: MIT
-- Rasklad: two quick taps of the left Alt (within 400 ms) switch the Fcitx layout
-- us <-> ru in every window. Right Alt is left alone: Omarchy binds it to dictation
-- push-to-talk. Load it from ~/.config/hypr/bindings.lua, only if installed:
--   local rasklad = "/usr/share/rasklad-fcitx5/hyprland/double-alt.lua"
--   local found = io.open(rasklad, "r")
--   if found then found:close(); dofile(rasklad) end
--
-- Detected here, not in Fcitx: after the first Alt, Electron apps such as Slack
-- move focus to their menu bar, and Fcitx never sees the second tap.
-- A plain release bind runs only when Alt was pressed and released alone, so
-- Alt+Tab or Alt+letter never count. It is non-consuming: applications still
-- get Alt (Slack's menu opens on the first tap and closes on the second).
local window_ms = 400
local armed = false
local expiry = nil

local function tap()
  if expiry then
    expiry:set_enabled(false)
    expiry = nil
  end
  if armed then
    armed = false
    hl.exec_cmd("rasklad-layout-toggle")
    return
  end
  armed = true
  expiry = hl.timer(function()
    armed = false
    expiry = nil
  end, { timeout = window_ms, type = "oneshot" })
end

for _, key in ipairs({ "ALT + ALT_L" }) do
  hl.bind(key, tap, { release = true, non_consuming = true, description = "Rasklad: switch layout (double Alt)" })
end
