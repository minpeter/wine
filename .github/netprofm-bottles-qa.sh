#!/bin/bash
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
# Requires sudo mount/network namespaces; never uses the user's Bottles data.
set -euo pipefail
scripts=$(realpath "$(dirname "$0")")
if [[ ${1-} != --inside ]]; then
    runner=$(realpath "${1:?usage: $0 RUNNER WIN32_OBSERVER SOURCE_TEST_DIR NEW_QA_DIR NEW_LOG_DIR}")
    observer=$(realpath "${2:?}")
    fixtures=$(realpath "${3:?}")
    mkdir "${4:?}" "${5:?}"
    qa=$(realpath "$4")
    logs=$(realpath "$5")
    file "$observer" | grep -F 'PE32 executable' | grep -F 'Intel 80386'
    component=$(basename "$runner")
    mkdir -p "$qa/data/bottles/runners" "$qa/data/bottles/bottles" "$qa/build/server" \
        "$qa/build/dlls/netprofm/tests/i386-windows" "$qa/fixture"
    if [[ $(stat -c %u "$runner/bin/wine") == 0 && $(id -u) != 0 ]]; then
        # Immutable root-owned files may be hard-linked without an in-place
        # write risk; only independent clone directories become user-owned.
        sudo -n cp -al "$runner" "$qa/data/bottles/runners/$component"
        sudo -n find "$qa/data/bottles/runners/$component" -type d \
            -exec chown "$(id -u):$(id -g)" {} +
    else
        cp -a --reflink=auto "$runner" "$qa/data/bottles/runners/$component"
    fi
    cp "$observer" "$qa/build/dlls/netprofm/tests/i386-windows/netprofm_test.exe"
    cp "$fixtures"/{dynamic-network.sh,reachability-network.sh,fake-networkmanager.py} "$qa/fixture/"
    sha256sum "$observer" "$fixtures/list.c" "$fixtures/../list.c" > "$logs/observer-source.sha256"
    ln -s "$scripts/netprofm-bottles-launch.sh" "$qa/build/wine"
    ln -s "$scripts/netprofm-bottles-launch.sh" "$qa/build/server/wineserver"
    flatpak=0
    if ! command -v bottles-cli >/dev/null; then
        flatpak info com.usebottles.bottles >/dev/null
        flatpak=1
    fi
    scratch=$(mktemp -d)
    chmod 1777 "$scratch"
    trap 'sudo -n rm -rf -- "$scratch"' EXIT
    sudo -n unshare --mount --propagation private bash -c '
        set -e
        mount --bind "$1" /tmp
        shift
        uid=$1 gid=$2 home=$3
        shift 3
        exec setpriv --reuid="$uid" --regid="$gid" --init-groups env -i \
            PATH=/usr/local/bin:/usr/bin:/bin HOME="$home" USER="$(getent passwd "$uid" | cut -d: -f1)" \
            XDG_RUNTIME_DIR="/run/user/$uid" LANG=C.UTF-8 LIBGL_ALWAYS_SOFTWARE=1 \
            dbus-run-session bash "$@"
    ' bash "$scratch" "$(id -u)" "$(id -g)" "$HOME" "$0" --inside "$qa" "$logs" "$flatpak"
    exit
fi

export SODA_QA_ROOT=$2 SODA_QA_FLATPAK=$4
logs=$3
SODA_QA_RUNNER=$(find "$SODA_QA_ROOT/data/bottles/runners" -mindepth 1 -maxdepth 1 -type d -print -quit)
export SODA_QA_RUNNER WINEARCH=win32 WINEDEBUG=-all
unset LD_PRELOAD LD_LIBRARY_PATH WINELOADER WINESERVER WINEDLLOVERRIDES
Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp -ac \
    3>"$logs/display" >"$logs/xvfb.log" 2>&1 &
xvfb_pid=$!
gui_pid=
cleanup()
{
    for prefix in /tmp/wine-netprofm-prefix /tmp/wine-netprofm-reachability-prefix-i386-windows; do
        if [[ -d $prefix ]]; then
            WINEPREFIX=$prefix "$SODA_QA_ROOT/build/server/wineserver" -k 2>/dev/null || true
        fi
    done
    [[ -z $gui_pid ]] || kill "$gui_pid" 2>/dev/null || true
    kill "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT
for ((attempt=0; attempt<100; attempt++)); do
    [[ ! -s $logs/display ]] || break
    sleep 0.1
done
[[ -s $logs/display ]]
export DISPLAY=:$(cat "$logs/display")
"$scripts/netprofm-bottles-launch.sh" settings set com.usebottles.bottles force-offline true
"$scripts/netprofm-bottles-launch.sh" cli list components > "$logs/runners.log" 2>&1
grep -F "$(basename "$SODA_QA_RUNNER")" "$logs/runners.log"
status=0
for fixture in dynamic-network reachability-network; do
    "$SODA_QA_ROOT/fixture/$fixture.sh" "$SODA_QA_ROOT/build" > "$logs/$fixture.log" 2>&1 &
    fixture_pid=$!
    sleep 5
    if [[ -z $gui_pid ]]; then
        "$scripts/netprofm-bottles-launch.sh" gui > "$logs/gui.log" 2>&1 &
        gui_pid=$!
        sleep 5
    fi
    python3 "$scripts/netprofm-capture-x11.py" "$logs/$fixture.png" || status=1
    if wait "$fixture_pid"; then
        grep -Eq ' tests executed \([^)]*, 0 failures\), [0-9]+ skipped\.' "$logs/$fixture.log" || status=1
        echo "Bottles observer completed: $fixture"
    else
        echo "Bottles observer FAILED: $fixture (see $logs/$fixture.log)"
        status=1
    fi
    if [[ $fixture == dynamic-network ]]; then
        WINEPREFIX=/tmp/wine-netprofm-prefix "$SODA_QA_ROOT/build/server/wineserver" -k 2>/dev/null || true
    fi
done
exit "$status"
