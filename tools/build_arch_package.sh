#!/usr/bin/env bash
# SYS-228: build the rasklad-fcitx5 Arch package from the committed HEAD.
# Never installs anything; prints the package path.
# Usage: tools/build_arch_package.sh [outdir]   (default build/arch-package)
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-"$root/build/arch-package"}
ver=$(sed -n 's/^pkgver=//p' "$root/packaging/arch/PKGBUILD")
if [[ -n $(git -C "$root" status --porcelain -- src tools addon packaging tests docs LICENSE) ]]; then
  echo 'refusing: uncommitted changes would not be in the package' >&2
  exit 1
fi
# Only this script's own output directory is replaced.
if [[ -e $out ]]; then
  find "$out" -mindepth 1 -delete
fi
mkdir -p "$out"
git -C "$root" archive --format=tar.gz --prefix="rasklad-${ver}/" -o "$out/rasklad-${ver}.tar.gz" HEAD
sum=$(sha256sum "$out/rasklad-${ver}.tar.gz" | cut -d' ' -f1)
sed "s/@SHA256@/${sum}/" "$root/packaging/arch/PKGBUILD" > "$out/PKGBUILD"
(cd "$out" && makepkg --cleanbuild --noconfirm --nodeps >/dev/null)
ls "$out"/rasklad-fcitx5-"${ver}"-*.pkg.tar.zst
