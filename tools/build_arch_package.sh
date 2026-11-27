#!/usr/bin/env bash
# SYS-228: build the rasklad-fcitx5 Arch package from the committed HEAD.
# Never installs anything; prints the package path.
# Usage: tools/build_arch_package.sh PROFILE_ROOT [outdir]   (default build/arch-package)
# PROFILE_ROOT holds the reviewed detector files at their build/... paths
# (packaging/arch/profile-files.txt); data/... files come from this commit.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
profile_root=${1:?usage: tools/build_arch_package.sh PROFILE_ROOT [outdir]}
profile_root=$(realpath -e "$profile_root")
out=${2:-"$root/build/arch-package"}
# The output directory is emptied: it must be a real directory inside build/.
mkdir -p "$root/build"
if [[ -L "$out" ]]; then echo 'refusing: output is a symlink' >&2; exit 1; fi
mkdir -p "$out"
out=$(realpath -e "$out")
case "$out/" in
  "$(realpath -e "$root/build")"/?*/) ;;
  *) echo 'refusing: output must be a directory inside build/' >&2; exit 1 ;;
esac
ver=$(sed -n 's/^pkgver=//p' "$root/packaging/arch/PKGBUILD")
if [[ -n $(git -C "$root" status --porcelain -- src tools addon packaging tests docs settings LICENSE) ]]; then
  echo 'refusing: uncommitted changes would not be in the package' >&2
  exit 1
fi
find "$out" -mindepth 1 -delete
git -C "$root" archive --format=tar.gz --prefix="rasklad-${ver}/" -o "$out/rasklad-${ver}.tar.gz" HEAD
sum=$(sha256sum "$out/rasklad-${ver}.tar.gz" | cut -d' ' -f1)
# Profile tarball: data/ files from the archived commit, build/ files from PROFILE_ROOT.
# check() runs the addon's own verifier on it (tests/profile_gate), and the addon
# checks it again when it loads.
stage=$(mktemp -d "$out/profile.XXXXXX")
while IFS= read -r rel; do
  [[ -z $rel || $rel == \#* ]] && continue
  if [[ $rel == /* || /$rel/ == */../* || /$rel/ == */./* ]]; then echo 'unsafe profile path' >&2; exit 1; fi
  case "$rel" in
    data/*) git -C "$root" show "HEAD:$rel" > "$stage/tmp" ;;
    build/*) [[ -f "$profile_root/$rel" && ! -L "$profile_root/$rel" ]] || { echo "missing profile file" >&2; exit 1; }
             cp "$profile_root/$rel" "$stage/tmp" ;;
    *) echo 'unexpected profile path' >&2; exit 1 ;;
  esac
  install -Dm644 "$stage/tmp" "$stage/profile/$rel"
done < "$root/packaging/arch/profile-files.txt"
rm "$stage/tmp"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@"$(git -C "$root" log -1 --format=%ct)" \
  -C "$stage" -czf "$out/rasklad-profile-${ver}.tar.gz" profile
find "$stage" -mindepth 1 -delete && rmdir "$stage"
psum=$(sha256sum "$out/rasklad-profile-${ver}.tar.gz" | cut -d' ' -f1)
sed -e "s/@SHA256@/${sum}/" -e "s/@PROFILE_SHA256@/${psum}/" "$root/packaging/arch/PKGBUILD" > "$out/PKGBUILD"
cp "$root/packaging/arch/rasklad-fcitx5.install" "$out/"  # SYS-270: pacman -R notice
# Same commit, same package: timestamps come from the commit.
SOURCE_DATE_EPOCH=$(git -C "$root" log -1 --format=%ct)
export SOURCE_DATE_EPOCH
# --check: check() runs the profile gate even if makepkg.conf disables checks.
(cd "$out" && makepkg --cleanbuild --check --noconfirm >/dev/null)
ls "$out"/rasklad-fcitx5-"${ver}"-*.pkg.tar.zst
