#!/bin/bash
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
# Exact pinned TKG preparation/build/package functions, with local source cache
# and a preinstalled isolated dependency environment instead of host apt/fetch.
set -o pipefail
[[ $(findmnt -n -o FSTYPE /) == overlay && $HOME == /work/home ]] || exit 1
packaging=/work/packaging
logs=$packaging/.amp/in/artifacts
git config --global --add safe.directory /inputs/wine/.git
git config --global --add safe.directory /inputs/port-source
_where=$packaging/.amp/in/release-tkg
srcdir=$_where/src
_winesrcdir=ValveSoftware-winegit
_stgsrcdir=absent-staging
pkgname=wine-tkg
msg() { printf '%s\n' "$*"; }
msg2() { printf '%s\n' "$*"; }
warning() { printf 'warning: %s\n' "$*"; }
error() { printf 'error: %s\n' "$*" >&2; }
source "$_where/wine-tkg-scripts/prepare.sh"
source "$_where/wine-tkg-scripts/build.sh"
source "$_where/wine-tkg-scripts/build-64.sh"
source "$_where/wine-tkg-scripts/build-32.sh"
trap - EXIT
cp "$logs/wine-tkg-valve.cfg" "$_where/wine-tkg-userpatches/user.cfg"
cat >> "$_where/wine-tkg-userpatches/user.cfg" <<'CFG'
_LOCAL_PRESET="valve-exp-bleeding"
_NOLIB32="false"
_ENABLE_TESTS="true"
_NOCCACHE="true"
_nomakepkg_dependency_autoresolver="false"
_nomakepkg_prefix_path="/work/packaging/.amp/in/release-install"
_configure_userargs64="SECRET_CFLAGS=-I/work/packaging/.amp/in/secret-amd64/include SECRET_LIBS=-L/work/packaging/.amp/in/secret-amd64/lib"
_configure_userargs32="--disable-wineopenxr SECRET_CFLAGS=-I/work/packaging/.amp/in/secret-i386/include SECRET_LIBS=-L/work/packaging/.amp/in/secret-i386/lib"
CFG
_init
_bleeding_tag=experimental-wine-bleeding-edge-11.0-428201-20260904-pdf1645-w0509a0-dd7ac25-v35bdee
[[ $_NOLIB32 == false && $_use_staging == false && $_wayland_driver == true && $_ENABLE_TESTS == true ]] || exit 1
_pkgnaming
pkgname=${pkgname/-faudio-git/}
mkdir -p "$srcdir"
if [[ ! -d $srcdir/$_winesrcdir/.git ]]; then
    git clone --shared --no-checkout /inputs/wine "$srcdir/$_winesrcdir" || exit
    printf '/inputs/port-source/.git/objects\n' >> "$srcdir/$_winesrcdir/.git/objects/info/alternates"
    git -C "$srcdir/$_winesrcdir" checkout --detach 0509a0b64a4852741364c95bcdc67ae8f74ef11d || exit
    # Independently replayed import checkpoint: exact MS-ICU + Proton wineopenxr.
    git -C "$srcdir/$_winesrcdir" read-tree --reset -u a816f85ec5126b4e39c01653be6b6c0c731aac3b || exit
    git -C "$srcdir/$_winesrcdir" reset --mixed HEAD || exit
    cp "$packaging"/wine-tkg-userpatches/*.mypatch "$_where/wine-tkg-userpatches/" || exit
    find "$_where/wine-tkg-userpatches" -type f -name '*.my*' -exec cp -n {} "$_where" \;
    cd "$srcdir/$_winesrcdir" || exit
    _configure_args=()
    _prepare || exit
    _polish || exit
fi
cd "$srcdir/$_winesrcdir" || exit
git -c safe.directory=/inputs/port-source -C /inputs/port-source diff-tree --no-commit-id --name-only -r 00c52fe7ff81792e76de6ac2a2c88177e1a38918 |
while IFS= read -r file; do cmp "$file" "/inputs/port-source/$file" || exit 1; done || exit
git diff --check || exit
pkgver=$(_describe_wine)
_NUKR=false
_makedirs || exit
mount --bind /work/build64 "$srcdir/$pkgname-64-build" || exit
mount --bind /work/build32 "$srcdir/$pkgname-32-build" || exit
_configure_args=()
_configure_args64=()
_configure_args32=()
_LAST_BUILD_CONFIG=$_where/last_build_config.log
_faudio_ignorecheck=true
_prebuild_common || exit
_nomakepkg_pkgname=$pkgname-$pkgver
_prefix=$_nomakepkg_prefix_path/$_nomakepkg_pkgname
_lib64name=lib
_lib32name=lib
_configure_args64+=($_configure_userargs64 --libdir="$_prefix/lib")
_configure_args32+=($_configure_userargs32 --libdir="$_prefix/lib")
declare -p pkgname pkgver _prefix _NOLIB32 _configure_args _configure_args64 _configure_args32 > "$logs/release-tkg-effective-config.txt"
for architecture in amd64 i386; do
    if [[ ! -L $packaging/.amp/in/secret-$architecture/lib/libsecret-1.so ]]; then
        bash "$packaging/.github/soda-secret-headers.sh" "$packaging/.amp/in/secret-$architecture" "$architecture" || exit
    fi
done
set -e
bash "$packaging/.github/netprofm-release-deps.sh" amd64 > "$logs/release-deps64.log" 2>&1
_exports_64
_configure_64 > "$logs/release-configure64.log" 2>&1
grep -q 'configure: Finished' "$logs/release-configure64.log"
_build_64 > "$logs/release-build64.log" 2>&1
bash "$packaging/.github/netprofm-release-deps.sh" i386 > "$logs/release-deps32.log" 2>&1
_exports_32
_configure_32 > "$logs/release-configure32.log" 2>&1
grep -q 'configure: Finished' "$logs/release-configure32.log"
_build_32 > "$logs/release-build32.log" 2>&1
_package_nomakepkg > "$logs/release-install.log" 2>&1
printf '%s\n' "$pkgdir" > "$logs/release-installed-path.txt"
cp "$_where/last_build_config.log" "$logs/release-tkg-last-build-config.txt"
cp "$_where/prepare.log" "$logs/release-tkg-prepare.log"
printf 'Recipe split x64/i386 build and TKG packaging completed.\n'
