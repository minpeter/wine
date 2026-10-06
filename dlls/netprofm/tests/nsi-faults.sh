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
work_dir=$(mktemp -d /tmp/wine-nsi-faults-XXXXXX)
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
wait_marker()
{
    count=0
    while [ ! -e "$work_dir/markers/$1" ]; do
        count=$((count + 1))
        if [ "$count" -ge 600 ] || ! kill -0 "$test_pid" 2>/dev/null; then
            cat "$work_dir/test.log"
            echo "missing marker $1" >&2
            exit 1
        fi
        sleep 0.1
    done
}

cc -shared -fPIC -Wall -Wextra -o "$work_dir/loader.so" "$source_dir/nsi-backend.c" -ldl
cc -c -fPIC -D__WINESRC__ -DWINE_UNIX_LIB -DNSI_FAULT_BACKEND \
    -I"$build_dir/include" -I"$root/include" \
    -o "$work_dir/nsi.o" "$source_dir/nsi-backend.c"
cc -shared -Wl,-Bsymbolic -Wl,-z,defs -o "$work_dir/nsiproxy.so" "$work_dir/nsi.o" \
    "$build_dir"/dlls/nsiproxy.sys/icmp_echo.o "$build_dir"/dlls/nsiproxy.sys/ip.o \
    "$build_dir"/dlls/nsiproxy.sys/ndis.o "$build_dir"/dlls/nsiproxy.sys/tcp.o \
    "$build_dir"/dlls/nsiproxy.sys/udp.o "$build_dir/dlls/ntdll/ntdll.so" -lpthread

ip link set lo up
ip link add nlm0 type dummy
sysctl -qw net.ipv6.conf.nlm0.keep_addr_on_down=1
mkdir "$prefix" "$work_dir/markers"
chmod 777 "$work_dir/markers"
if [ -n "$run_uid" ]; then chown "$run_uid:$run_gid" "$prefix"; fi
# Bootstrap before injecting a blocked polling worker into the device host.
run_as_user env WINEPREFIX="$prefix" DBUS_SYSTEM_BUS_ADDRESS="unix:path=$work_dir/no-bus" \
    "$build_dir/wine" wineboot --init >"$work_dir/test.log" 2>&1
run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -w

arches=x86_64
if [ -f "$build_dir/dlls/netprofm/tests/i386-windows/netprofm_test.exe" ]; then arches="x86_64 i386"; fi
for arch in $arches; do
    target=x86_64-w64-mingw32
    [ "$arch" != i386 ] || target=i686-w64-mingw32
    set -- --cc-cmd="$target-gcc" -b "$target" -D__WINE_PE_BUILD -lcompiler-rt
    test_exe=$work_dir/nsi-$arch.exe
    if [ -f "$build_dir/dlls/netprofm/tests/netprofm_test.exe.so" ]; then
        set -- --cc-cmd=gcc -m64 -mno-cygwin -fPIC -D_WIN32 -mlong-double-64 \
            -fno-builtin -fshort-wchar -mabi=ms -fno-stack-protector
        test_exe=$test_exe.so
    fi
    "$build_dir/tools/winegcc/winegcc" --wine-objdir "$build_dir" "$@" \
        -D__WINESRC__ -D_MSVCR_VER=0 -I"$build_dir/include" \
        -I"$root/include" -I"$root/include/msvcrt" -I"$source_dir/.." \
        -o "$test_exe" "$source_dir/nsi-faults.c" \
        -luuid -liphlpapi -lnsi -lole32 -lwinecrt0 -lmsvcrt -lkernel32 -lntdll
    device_exe=$work_dir/device-$arch.exe
    ntoskrnl=$build_dir/dlls/ntoskrnl.exe/$arch-windows/libntoskrnl.a
    case "$test_exe" in *.so)
        device_exe=$device_exe.so
        ntoskrnl=$build_dir/dlls/ntoskrnl.exe/libntoskrnl.a;;
    esac
    "$build_dir/tools/winegcc/winegcc" --wine-objdir "$build_dir" "$@" \
        -D__WINESRC__ -D_MSVCR_VER=0 -I"$build_dir/include" \
        -I"$root/include" -I"$root/include/msvcrt" \
        -o "$device_exe" "$source_dir/nsi-device.c" \
        -luuid "$ntoskrnl" -lwinecrt0 -lmsvcrt -lkernel32 -lntdll
    echo "NSI device interleavings: $arch"
    result=0
    run_as_user env WINEPREFIX="$prefix" "$build_dir/wine" "$device_exe" \
        >"$work_dir/test.log" 2>&1 || result=$?
    cat "$work_dir/test.log"
    [ "$result" -eq 0 ] || exit "$result"
    run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -k
    run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -w
    for mode in socket recv retained control; do
        rm -f "$work_dir/markers/"*
        ip addr flush dev nlm0
        ip addr add 192.0.2.2/24 dev nlm0
        ip -6 addr add 2001:db8::2/64 dev nlm0 nodad
        ip link set nlm0 up
        ip route add default via 192.0.2.1 dev nlm0
        echo "NSI backend: $arch $mode"
        run_as_user env WINEPREFIX="$prefix" WINETEST_NSI_FAULT="$mode" \
            WINETEST_NSI_DIR="$work_dir/markers" WINETEST_NSI_BACKEND="$work_dir/nsiproxy.so" \
            LD_PRELOAD="$work_dir/loader.so" DBUS_SYSTEM_BUS_ADDRESS="unix:path=$work_dir/no-bus" \
            "$build_dir/wine" "$test_exe" >"$work_dir/test.log" 2>&1 &
        test_pid=$!
        wait_marker polling
        wait_marker ready
        ip link set nlm0 down
        # Cover both the legacy retained-address snapshot and no-address
        # disconnected snapshot; neither may claim to remain dynamic.
        [ "$mode" = retained ] || ip addr flush dev nlm0
        touch "$work_dir/markers/go" "$work_dir/markers/changed"
        result=0
        wait "$test_pid" || result=$?
        cat "$work_dir/test.log"
        [ "$mode" = control ] || test -f "$work_dir/markers/injected"
        case "$mode" in recv|retained) test -f "$work_dir/markers/closed";; esac
        run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -k
        run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -w
        [ "$result" -eq 0 ] || exit "$result"
    done
done
