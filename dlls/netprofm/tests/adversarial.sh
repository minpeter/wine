#!/bin/sh
set -eu

if [ "${1-}" != "--inside" ]; then
    build_dir=${1:?usage: $0 BUILD_DIR}
    build_dir=$(CDPATH= cd -- "$build_dir" && pwd)
    if unshare --user --map-root-user --net true 2>/dev/null; then
        exec unshare --user --map-root-user --net "$0" --inside "$build_dir"
    fi
    if command -v sudo >/dev/null && sudo -n true 2>/dev/null; then
        exec sudo unshare --net "$0" --inside "$build_dir" "$(id -u)" "$(id -g)"
    fi
    echo "network namespaces are unavailable" >&2
    exit 1
fi

build_dir=$2
run_uid=${3-}
run_gid=${4-}
source_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
root=$(CDPATH= cd -- "$source_dir/../../.." && pwd)
work_dir=$(mktemp -d /tmp/wine-netprofm-adversarial-XXXXXX)
chmod 755 "$work_dir"
prefix=$work_dir/prefix
run_as_user()
{
    if [ -n "$run_uid" ]; then
        run_home=$(getent passwd "$run_uid" | cut -d: -f6)
        setpriv --reuid="$run_uid" --regid="$run_gid" --init-groups env HOME="$run_home" "$@"
    else
        "$@"
    fi
}
cleanup()
{
    run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -k 2>/dev/null || true
    run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -w 2>/dev/null || true
    rm -rf "$work_dir"
}
trap cleanup EXIT INT TERM

ip link set lo up
ip link add nlm0 type dummy
ip addr add 192.0.2.2/24 dev nlm0
ip -6 addr add 2001:db8::2/64 dev nlm0 nodad
ip link set nlm0 up
ip route add 198.51.100.0/24 via 192.0.2.1 dev nlm0
mkdir "$prefix"
if [ -n "$run_uid" ]; then chown "$run_uid:$run_gid" "$prefix"; fi

cc -shared -fPIC -Wall -Wextra -o "$work_dir/loader.so" "$source_dir/topology-backend.c" -ldl
dbus=false
if grep -q '^#define SONAME_LIBDBUS_1 ' "$build_dir/include/config.h"; then
    dbus=true
    cc -shared -fPIC -Wall -Wl,-z,defs -D__WINESRC__ -DWINE_UNIX_LIB \
        -I"$build_dir/include" -I"$root/include" -I"$source_dir/.." \
        $(pkg-config --cflags dbus-1) -o "$work_dir/dbus.so" "$source_dir/dbus-malformed.c" \
        "$build_dir/dlls/ntdll/ntdll.so" $(pkg-config --libs dbus-1) -lpthread
fi

arches=x86_64
if [ -f "$build_dir/dlls/netprofm/tests/i386-windows/netprofm_test.exe" ]; then arches="x86_64 i386"; fi
for arch in $arches; do
    target=x86_64-w64-mingw32
    [ "$arch" != i386 ] || target=i686-w64-mingw32
    set -- --cc-cmd="$target-gcc" -b "$target" -D__WINE_PE_BUILD -lcompiler-rt
    monitor_exe=$work_dir/monitor-$arch.exe
    if [ -f "$build_dir/dlls/netprofm/tests/netprofm_test.exe.so" ]; then
        set -- --cc-cmd=gcc -m64 -mno-cygwin -fPIC -D_WIN32 -mlong-double-64 \
            -fno-builtin -fshort-wchar -mabi=ms -fno-stack-protector
        monitor_exe=$monitor_exe.so
    fi
    "$build_dir/tools/winegcc/winegcc" --wine-objdir "$build_dir" "$@" \
        -D__WINESRC__ -D_MSVCR_VER=0 -I"$build_dir/include" \
        -I"$root/include" -I"$root/include/msvcrt" -I"$source_dir/.." \
        -o "$monitor_exe" "$source_dir/monitor-faults.c" \
        -luuid -liphlpapi -lnsi -lole32 -lwinecrt0 -lmsvcrt -lkernel32 -lntdll
    echo "monitor faults: $arch"
    result=0
    run_as_user env WINEPREFIX="$prefix" "$build_dir/wine" "$monitor_exe" \
        >"$work_dir/test.log" 2>&1 || result=$?
    cat "$work_dir/test.log"
    [ "$result" -eq 0 ] || exit "$result"
    run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -w
    if "$dbus"; then
        test_exe=$build_dir/dlls/netprofm/tests/$arch-windows/netprofm_test.exe
        if [ -f "$build_dir/dlls/netprofm/tests/netprofm_test.exe.so" ]; then
            test_exe=$build_dir/dlls/netprofm/tests/netprofm_test.exe.so
        fi
        echo "D-Bus adversarial: $arch"
        result=0
        run_as_user env WINEPREFIX="$prefix" WINETEST_NETPROFM_TOPOLOGY_POLICY=dynamic-up \
            WINETEST_NETPROFM_BACKEND="$work_dir/dbus.so" LD_PRELOAD="$work_dir/loader.so" \
            DBUS_SYSTEM_BUS_ADDRESS="unix:path=$work_dir/no-bus" \
            "$build_dir/wine" "$test_exe" list >"$work_dir/test.log" 2>&1 || result=$?
        cat "$work_dir/test.log"
        [ "$result" -eq 0 ] || exit "$result"
        grep -q 'D-Bus adversarial fixture: .* checks passed' "$work_dir/test.log"
        run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -w
    fi
done
