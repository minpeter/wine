#!/bin/bash
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
# Disposable Ubuntu host-base COW rootfs; never runs apt against the host root.
set -euo pipefail
packaging=$(realpath "$(dirname "$0")/..")
state=$packaging/.amp/in/release-rootfs
if [[ ${1-} != --inside ]]; then
    mkdir -p "$state/root"
    if [[ ! -f $state/upper.path ]]; then
        mktemp -d /dev/shm/soda-nlm-release-rootfs.XXXXXX > "$state/upper.path"
    fi
    upper=$(cat "$state/upper.path")
    [[ $upper == /dev/shm/soda-nlm-release-rootfs.* && -d $upper ]]
    mkdir -p "$upper/upper" "$upper/work"
    exec sudo -n unshare --mount --pid --fork --propagation private \
        bash "$0" --inside "$upper" "$@"
fi
shift
upper=$1
shift
root=$state/root
mount -t overlay overlay -o "lowerdir=/,upperdir=$upper/upper,workdir=$upper/work" "$root"
mount -t tmpfs tmpfs "$root/run"
mount -t tmpfs tmpfs "$root/tmp"
mount -t tmpfs tmpfs "$root/home"
mount -t tmpfs tmpfs "$root/root"
rm -f "$root/etc/resolv.conf"
cp -L /etc/resolv.conf "$root/etc/resolv.conf"
mkdir -p "$root/work/packaging" "$root/inputs/wine" "$root/inputs/port-source"
mount --bind "$packaging" "$root/work/packaging"
for bits in 64 32; do
    mkdir -p "$root/work/build$bits"
    mount --bind "$(realpath "$packaging/../wine-soda-nlm-build$bits")" "$root/work/build$bits"
    tkg_build=$root/work/packaging/.amp/in/release-tkg/src/wine-tkg-valve-exp-bleeding-$bits-build
    if [[ -d $tkg_build ]]; then mount --bind "$root/work/build$bits" "$tkg_build"; fi
done
for input in wine port-source; do
    if [[ $input == wine ]]; then source=$packaging/../wine-nlm-connectivity;
    else source=$packaging/../wine-soda-nlm-source; fi
    mount --bind "$(realpath "$source")" "$root/inputs/$input"
    mount -o remount,bind,ro "$root/inputs/$input"
done
mount -t proc proc "$root/proc"
mount -t tmpfs tmpfs "$root/dev"
for device in null zero random urandom tty; do
    touch "$root/dev/$device"
    mount --bind "/dev/$device" "$root/dev/$device"
done
mkdir -p "$root/dev/pts" "$root/dev/shm"
mount -t devpts devpts -o newinstance,ptmxmode=0666 "$root/dev/pts"
ln -s pts/ptmx "$root/dev/ptmx"
ln -s /proc/self/fd "$root/dev/fd"
mount -t tmpfs tmpfs "$root/dev/shm"
mkdir -p "$root/work/home"
exec chroot "$root" /usr/bin/env -i \
    PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    HOME=/work/home USER=root LOGNAME=root LANG=C.UTF-8 \
    DEBIAN_FRONTEND=noninteractive \
    /bin/bash -c 'cd /work/packaging; exec "$@"' bash "$@"
