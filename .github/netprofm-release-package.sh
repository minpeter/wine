#!/bin/bash
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
set -euo pipefail
[[ $(findmnt -n -o FSTYPE /) == overlay && $HOME == /work/home ]]
packaging=/work/packaging
logs=$packaging/.amp/in/artifacts
component=soda-11.0-28-nlm-local-20261008-x86_64
destination=$packaging/.amp/in/release-install/$component
mode=${1:?usage: netprofm-release-package.sh prepare|archive}
[[ $mode == prepare || $mode == archive ]]

if [[ $mode == prepare ]]; then
    runner=$(cat "$logs/release-installed-path.txt")
    [[ $runner == "$packaging/.amp/in/release-install/"* && -d $runner ]]
    [[ ! -e $destination ]]
    for architecture in i386 x86_64; do
        unix_dir=$runner/lib/wine/$architecture-unix
        for module in ntdll win32u winebus winevulkan winex11 winegstreamer winscard netprofm nsiproxy; do
            [[ -f $unix_dir/$module.so ]]
        done
        [[ $(find "$unix_dir" -maxdepth 1 -name '*.so' -type f | wc -l) -ge 30 ]]
        grep -aFq org.winehq.Soda.Identity "$unix_dir/windows.security.authentication.onlineid.so"
        for module in winegstreamer winscard windows.security.authentication.onlineid twinapi.appcore; do
            [[ -f $runner/lib/wine/$architecture-windows/$module.dll ]]
        done
    done
    file "$runner/bin/wine" | grep -F x86-64
    file "$runner/lib/wine/i386-windows/ntdll.dll" | grep -F 'Intel 80386'
    [[ -f $runner/lib/wine/x86_64-unix/wineopenxr.so ]]
    [[ -f $runner/lib/wine/x86_64-windows/wineopenxr.dll ]]
    install -Dm644 "$packaging/.amp/in/proton/wineopenxr/wineopenxr64.json" "$runner/share/openxr/wineopenxr64.json"
    mono=wine-mono-10.4.1-x86.msi
    mono_sha=071f4b2887e1c97a11d791ff3d65be9429eed6dec4c2708888bfd546ba358e23
    mkdir -p "$packaging/.amp/in/downloads"
    if [[ ! -f $packaging/.amp/in/downloads/$mono ]]; then
        curl --fail --location --retry 3 --output "$packaging/.amp/in/downloads/$mono" \
            "https://github.com/wine-mono/wine-mono/releases/download/wine-mono-10.4.1/$mono"
    fi
    printf '%s  %s\n' "$mono_sha" "$packaging/.amp/in/downloads/$mono" | sha256sum --check -
    install -Dm644 "$packaging/.amp/in/downloads/$mono" "$runner/share/wine/mono/$mono"
    xvfb-run -a sh "$packaging/.github/build-eagle.sh" "$runner" \
        "$packaging/.amp/in/release-eagle" > "$logs/release-eagle.log" 2>&1
    # Only the prefix this invocation created; retain all Eagle evidence.
    WINEPREFIX=$packaging/.amp/in/release-eagle/tests/prefix "$runner/bin/wineserver" -k || true
    WINEPREFIX=$packaging/.amp/in/release-eagle/tests/prefix "$runner/bin/wineserver" -w
    rm -rf "$packaging/.amp/in/release-eagle/tests/prefix"
    licenses=$runner/share/soda-nlm/licenses
    mkdir -p "$licenses"
    for document in AUTHORS COPYING.LIB LICENSE LICENSE.OLD; do
        cp "/inputs/port-source/$document" "$licenses/$document"
    done
    while IFS= read -r -d '' document; do
        relative=${document#/inputs/port-source/}
        install -Dm644 "$document" "$licenses/$relative"
    done < <(find /inputs/port-source/libs -type f \( -iname '*license*' -o -iname 'copying*' \) -print0)
    cp "$packaging/.github/netprofm-port.md" "$runner/share/soda-nlm/README.md"
    cp "$packaging/wine-tkg-userpatches/netprofm-dynamic-connectivity.mypatch" "$runner/share/soda-nlm/"
    mv "$runner" "$destination"
    printf '%s\n' "$destination" > "$logs/release-local-runner-path.txt"
    echo 'Local runner assembled; archive only after final QA and provenance update.'
    exit
fi

[[ -d $destination ]]
status=$(git -c safe.directory="$packaging" -C "$packaging" status --porcelain)
[[ -z $status ]]
provenance=$destination/share/soda-nlm
cp "$packaging/.github/netprofm-port.md" "$provenance/README.md"
git -c safe.directory="$packaging" -C "$packaging" rev-parse HEAD > "$provenance/packaging-commit.txt"
git -c safe.directory=/inputs/port-source -C /inputs/port-source rev-parse HEAD HEAD^ > "$provenance/source-commits.txt"
cp "$logs"/release-source-revisions.txt "$logs"/release-replay-*.txt \
    "$logs"/release-tkg-effective-config.txt "$logs"/release-rootfs-packages-*.txt \
    "$provenance/"
cp /etc/os-release "$provenance/build-os-release"
cp "$logs"/release-configure64.log "$logs"/release-configure32.log "$provenance/"
cp "$logs"/release-runtime-linkage-resolved.txt "$logs"/release-runtime-dlopen.txt \
    "$logs"/release-rootfs-ubuntu.sources "$logs"/release-eagle.log \
    "$logs"/release-final-tkg-audit.log "$logs"/release-installed-*.log \
    "$logs"/release-smokes.log "$provenance/"
cp -a "$logs"/release-final-tests64 "$logs"/release-final-split32 \
    "$logs"/release-smokes "$provenance/"
(cd "$destination"; find . -type l -printf '%p -> %l\n' | LC_ALL=C sort) > "$provenance/SYMLINKS.txt"
(cd "$destination"; find . -type f ! -path './share/soda-nlm/FILES.sha256' -print0 |
    LC_ALL=C sort -z | xargs -0 sha256sum) > "$provenance/FILES.sha256"
out=$packaging/.amp/in/artifacts/handoff
mkdir -p "$out"
[[ ! -e $out/$component.tar.xz ]]
tar --sort=name --owner=0 --group=0 --numeric-owner -C "$(dirname "$destination")" \
    -cJf "$out/$component.tar.xz" "$component"
split --bytes=8M --numeric-suffixes=1 --suffix-length=3 \
    "$out/$component.tar.xz" "$out/$component.tar.xz.part-"
(cd "$out"; sha256sum "$component.tar.xz" "$component.tar.xz.part-"*) > "$out/SHA256SUMS"
(cd "$out"; sha256sum --check SHA256SUMS)
printf 'Artifact: %s\n' "$out/$component.tar.xz"
