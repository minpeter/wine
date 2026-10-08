#!/bin/bash
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
set -euo pipefail
[[ $(findmnt -n -o FSTYPE /) == overlay && $HOME == /work/home ]]
logs=/work/packaging/.amp/in/artifacts
arch=${1:-amd64}
[[ $arch == amd64 || $arch == i386 ]]
printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d
chmod 755 /usr/sbin/policy-rc.d
rm -f /etc/apt/sources.list.d/google-chrome.sources
dpkg --add-architecture i386
apt-get update
apt-get -f install -y -o Dpkg::Options::="--force-overwrite"
# Current Noble names for the pinned TKG's development libraries. Obsolete
# prelink/FFmpeg SONAME/LLVM12 packages are not build feature switches.
libraries=(asound2 capi20 cups2 dbus-1 fontconfig freetype6 gif glu1-mesa
    gnutls28 gphoto2 gsm1 gstreamer1.0 gudev-1.0
    krb5 lcms2 ldap lzma mpg123 ncurses openal osmesa6 pcap pcsclite png
    pulse sane sdl2 ssl tiff udev usb-1.0-0 v4l va vulkan wayland
    x11 xcomposite xcursor xext xi xinerama xkbcommon xkbregistry xml2
    xrandr xrender xslt1 xt xxf86vm avcodec avformat avutil swresample unwind)
packages=(gcc-multilib g++-multilib g++-mingw-w64-i686 g++-mingw-w64-x86-64
    autoconf bison flex gettext make pkg-config cmake ninja-build git curl
    xz-utils patch python3-pefile python3-dbus python3-gi dbus-x11 xvfb
    libglib2.0-dev-bin libwayland-bin glslang-dev glslang-tools
    libsecret-1-0:amd64 libsecret-1-0:i386
    libopenxr-dev:amd64 ocl-icd-opencl-dev:amd64 ocl-icd-opencl-dev:i386)
# These Noble development packages are not co-installable. Like TKG's serial
# dependency resolver, switch them only after the previous architecture builds.
packages+=("libgcrypt20-dev:$arch" "libgstreamer-plugins-base1.0-dev:$arch"
    "libsecret-1-dev:$arch" "samba-dev:$arch" "samba-libs:$arch")
for library in "${libraries[@]}"; do
    packages+=("lib$library-dev:amd64" "lib$library-dev:i386")
done
# The recipe uses this for Noble's mismatched shared multiarch dev files.
apt-get install --no-install-recommends -y -o Dpkg::Options::="--force-overwrite" "${packages[@]}"
apt-get clean
apt-get check
dpkg-query -W -f='${binary:Package}\t${Version}\n' > "$logs/release-rootfs-packages-$arch.txt"
cp /etc/apt/sources.list.d/ubuntu.sources "$logs/release-rootfs-ubuntu.sources"
printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o /tmp/soda-m32
file /tmp/soda-m32
/tmp/soda-m32
printf 'Split-i386 toolchain link and execution passed.\n'
