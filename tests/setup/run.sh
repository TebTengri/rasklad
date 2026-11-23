#!/bin/bash
# SPDX-License-Identifier: MIT
# SYS-252: run rasklad-setup against a clean copy of Omarchy's stock profile in a
# sandbox (no network, empty home, its own D-Bus and Fcitx, a fake hyprctl).
# Nothing of the host's home is visible. Prints the JSON line from inside.py.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/home"

# hyprctl stand-in: two layouts and a group toggle, as Omarchy's stock profile,
# until our block is in input.lua; no config errors.
cat > "$work/hyprctl" <<'EOF'
#!/bin/bash
input=$XDG_CONFIG_HOME/hypr/input.lua
case "$1 $2" in
  "getoption input:kb_layout")
    if grep -q "BEGIN rasklad-setup" "$input"; then echo '{"str": "us"}'; else echo '{"str": "us,ru"}'; fi ;;
  "getoption input:kb_options")
    if grep -q "BEGIN rasklad-setup" "$input"; then echo '{"str": "compose:caps"}'
    else echo '{"str": "compose:caps,grp:alts_toggle"}'; fi ;;
  "getoption input:kb_variant") echo '{"str": ","}' ;;
  "configerrors -j") echo '[""]' ;;
  "version -j"|"reload "*|"reload") echo '{}' ;;
  *) exit 1 ;;
esac
EOF
chmod 755 "$work/hyprctl"

cat > "$work/inside.sh" <<'EOF'
#!/bin/bash
set -e
export HOME=/home/lab XDG_CONFIG_HOME=/home/lab/.config XDG_STATE_HOME=/home/lab/.local/state
export XDG_DATA_HOME=/home/lab/.local/share XDG_RUNTIME_DIR=/home/lab/run
mkdir -p $XDG_RUNTIME_DIR $XDG_CONFIG_HOME/hypr $XDG_CONFIG_HOME/omarchy/plugins $XDG_CONFIG_HOME/fcitx5
chmod 700 $XDG_RUNTIME_DIR
# A clean profile: Omarchy's stock files (read-only originals, copied).
cp /usr/share/omarchy/config/hypr/bindings.lua /usr/share/omarchy/config/hypr/input.lua $XDG_CONFIG_HOME/hypr/
cp /usr/share/omarchy/config/omarchy/shell.json $XDG_CONFIG_HOME/omarchy/
printf '[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=keyboard-us\n[Groups/0/Items/0]\nName=keyboard-us\nLayout=\n[GroupOrder]\n0=Default\n' > $XDG_CONFIG_HOME/fcitx5/profile
exec dbus-run-session -- bash -c '
  fcitx5 --disable=wayland,xcb,waylandim,xim,ibusfrontend,notificationitem,clipboard -d >/dev/null 2>&1
  for i in $(seq 50); do busctl --user status org.fcitx.Fcitx5 >/dev/null 2>&1 && break; sleep 0.1; done
  sleep 0.5
  python3 -I /mnt/inside.py; code=$?
  fcitx5-remote -e >/dev/null 2>&1 || true
  exit $code'
EOF
chmod 755 "$work/inside.sh"

bwrap --unshare-all --die-with-parent --ro-bind / / --tmpfs /home --bind "$work/home" /home/lab \
  --tmpfs /tmp --dev /dev --proc /proc --tmpfs /mnt \
  --ro-bind "$repo/packaging/arch/rasklad-setup" /mnt/rasklad-setup \
  --ro-bind "$repo/tests/setup/inside.py" /mnt/inside.py \
  --ro-bind "$work/inside.sh" /mnt/inside.sh \
  --ro-bind "$work/hyprctl" /usr/bin/hyprctl \
  /mnt/inside.sh 2>/dev/null | grep '^{'
