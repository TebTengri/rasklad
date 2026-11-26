#!/bin/bash
# SPDX-License-Identifier: MIT
# SYS-258: the automatic checks, as GitHub Actions runs them (.github/workflows/ci.yml)
# and as anyone can run them locally from a clean checkout. Builds the product the way
# PKGBUILD does, runs every unit test that needs no screen, no Fcitx session, no
# dictionary profile and no network, and lints the scripts. The Sway/Fcitx stand and the
# profile gate stay local: CI never sees the profile or any real input.
# usage: tools/ci.sh            (from the repository root; builds into build/ci/)
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
out=build/ci
mkdir -p "$out"
jobs=$(nproc 2>/dev/null || echo 2)
step() { printf '\n==> %s\n' "$*"; }

step 'product build (as PKGBUILD build())'
for spec in addon:tools/fcitx_product.pro policy:tests/application_policy/application_policy.pro \
            permissions:tests/application_permissions/application_permissions.pro \
            cue:tests/layout_cue/layout_cue.pro settings:settings/settings.pro \
            settings-tests:tests/settings/settings_tests.pro \
            desktopindex-tests:tests/settings/desktopindex_tests.pro \
            core:tests/product_core/product_core.pro gesture:tests/manual_gesture/manual_gesture.pro; do
  dir=$out/${spec%%:*}
  mkdir -p "$dir"
  (cd "$dir" && qmake6 "$root/${spec#*:}" >/dev/null && make -j"$jobs" >/dev/null)
done

step 'product unit tests (as PKGBUILD check())'
export QT_QPA_PLATFORM=offscreen
"$out"/policy/application_policy_tests
"$out"/permissions/application_permissions_tests
"$out"/cue/layout_cue_tests
"$out"/settings-tests/settings_tests
"$out"/desktopindex-tests/desktopindex_tests
"$out"/core/product_core_tests
"$out"/gesture/manual_gesture_tests

step 'addon imports and build flags'
bash packaging/arch/check-imports.sh "$out"/addon/rasklad.so
# Only the product's own macros reach the addon build (lab variants stay out; the
# source also refuses them with #error).
flags=$(grep -E '^DEFINES[[:space:]]*=' "$out"/addon/Makefile | grep -oE -- '-DRASKLAD_[A-Z_]+' | sort -u | tr '\n' ' ')
[[ $flags == '-DRASKLAD_MANUAL_LAB -DRASKLAD_PRODUCT -DRASKLAD_WITH_HUNSPELL ' ]] ||
  { echo "unexpected addon macros: $flags" >&2; exit 1; }
echo "macros: $flags"

step 'core, research and app tests (bin/test)'
bin/test

step 'packaging tests'
python3 -I -m unittest discover -s tests/packaging

step 'rasklad-setup sandbox'
bash tests/setup/run.sh

step 'scripts'
# GitHub Actions sets CI=true and installs shellcheck; a local run without it skips.
if command -v shellcheck >/dev/null; then
  shellcheck -S warning packaging/arch/rasklad-layout-toggle packaging/arch/check-imports.sh \
    tools/build_arch_package.sh tools/ci.sh tests/setup/run.sh
elif [[ ${CI:-} == true ]]; then
  echo 'shellcheck is required in CI' >&2; exit 1
else
  echo 'shellcheck: not installed, skipped locally'
fi
luac -p packaging/hyprland/double-alt.lua
python3 -I -c 'import ast, sys; [ast.parse(open(f, encoding="utf-8").read(), f) for f in sys.argv[1:]]' \
  packaging/arch/rasklad-setup packaging/arch/rasklad-layout-watch \
  packaging/arch/rasklad-pause-toggle tools/update_dictionaries.py tools/make_release_profile.py

step 'all checks passed'
