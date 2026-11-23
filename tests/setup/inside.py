# SPDX-License-Identifier: MIT
# SYS-252 acceptance, run inside tests/setup/run.sh's sandbox: on a clean
# Omarchy profile --check reports missing steps, --install applies them, --check
# is then clean, a second --install changes nothing, and --revert returns every
# file byte for byte. Prints one JSON line of booleans and counts only.
import hashlib
import json
import os
import subprocess
import sys

TOOL = "/mnt/rasklad-setup"
CONFIG = os.environ["XDG_CONFIG_HOME"]
STATE = os.path.join(os.environ["XDG_STATE_HOME"], "rasklad-setup")


def snapshot():
  out = {}
  for root, dirs, files in os.walk(CONFIG):
    for name in dirs + files:
      path = os.path.join(root, name)
      if os.path.islink(path):
        out[path] = "link:" + os.readlink(path)
      elif os.path.isfile(path):
        with open(path, "rb") as f:
          out[path] = hashlib.sha256(f.read()).hexdigest()
  return out


def tool(*args):
  done = subprocess.run([TOOL] + list(args), capture_output=True, text=True, timeout=60)
  states = {}
  for line in done.stdout.splitlines():
    parts = line.split()
    if len(parts) >= 2 and not line.startswith(("+", "-", "@", " ")):
      states[parts[1]] = parts[0]
  return done.returncode, states


def save():
  # Fcitx rewrites its own profile in its own format; ask it to write now so
  # every snapshot compares that format with itself.
  subprocess.run(["busctl", "--user", "call", "org.fcitx.Fcitx5", "/controller",
                  "org.fcitx.Fcitx.Controller1", "Save"], capture_output=True, timeout=10)


def group():
  done = subprocess.run(["busctl", "--user", "--json=short", "call", "org.fcitx.Fcitx5", "/controller",
                         "org.fcitx.Fcitx.Controller1", "InputMethodGroupInfo", "s", "Default"],
                        capture_output=True, text=True, timeout=10)
  try:
    reply = json.loads(done.stdout)["data"]
    return [reply[0], [list(i) for i in reply[1]]]  # layout and items, in order
  except (ValueError, KeyError, IndexError, TypeError):
    return None


report = {}
save()
before = snapshot()
group_before = group()
code, states = tool("--check")
report["checkMissingFirst"] = code == 1 and states.get("double-alt") == "missing" and \
  states.get("hyprland-layout") == "missing" and states.get("bar-widget") == "missing" and \
  states.get("bar-widget-link") == "missing" and states.get("fcitx-group") == "missing"
code, _ = tool("--install", "--yes")
report["installOk"] = code == 0
after_install = snapshot()
code, states = tool("--check")
steps = ("package", "fcitx-addon", "fcitx-group", "double-alt", "hyprland-layout", "bar-widget-link", "bar-widget")
report["checkCleanAfter"] = code == 0 and all(states.get(s) == "ok" for s in steps)
now = group()
report["groupHasBoth"] = now is not None and [i[0] for i in now[1]] == ["keyboard-us", "keyboard-ru"]
# Both edited Lua files still compile.
report["luaValid"] = all(subprocess.run(["luac", "-p", os.path.join(CONFIG, "hypr", f)],
                                        capture_output=True).returncode == 0
                         for f in ("bindings.lua", "input.lua"))
code, _ = tool("--install", "--yes")
report["secondInstallNoChange"] = code == 0 and snapshot() == after_install
report["bakBeside"] = os.path.isfile(os.path.join(CONFIG, "hypr", "bindings.lua.rasklad.bak"))
report["filesChanged"] = sum(1 for p in set(before) | set(after_install) if before.get(p) != after_install.get(p))
code, _ = tool("--revert")
report["revertOk"] = code == 0
save()
report["revertByteExact"] = snapshot() == before
report["groupRestored"] = group() == group_before
report["stateRemoved"] = not os.path.exists(STATE)
code, _ = tool("--revert")
save()
report["secondRevertNoop"] = code == 0 and snapshot() == before
# A config that is a symlink (a dotfiles checkout) is never written.
bindings = os.path.join(CONFIG, "hypr", "bindings.lua")
real = os.path.join(os.environ["HOME"], "dotfiles-bindings.lua")
os.replace(bindings, real)
os.symlink(real, bindings)
with open(real, "rb") as f:
  real_before = f.read()
code, states = tool("--install", "--yes")
with open(real, "rb") as f:
  report["symlinkLeftAlone"] = os.path.islink(bindings) and f.read() == real_before and \
    states.get("bindings.lua") == "symlink"
tool("--revert")
required = ["checkMissingFirst", "installOk", "checkCleanAfter", "groupHasBoth", "luaValid",
            "secondInstallNoChange", "bakBeside", "revertOk", "revertByteExact", "groupRestored",
            "stateRemoved", "secondRevertNoop", "symlinkLeftAlone"]
report["checks"] = len(required)
report["passed"] = all(report[k] is True for k in required)
print(json.dumps(report, sort_keys=True))
sys.exit(0 if report["passed"] else 1)
