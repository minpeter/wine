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
test_dir=$build_dir/dlls/netprofm/tests
if [ -f "$test_dir/netprofm_test.exe.so" ]; then
    test_exes=$test_dir/netprofm_test.exe.so
elif [ -f "$test_dir/netprofm_test.exe" ]; then
    test_exes=$test_dir/netprofm_test.exe
else
    test_exes="$test_dir/x86_64-windows/netprofm_test.exe $test_dir/i386-windows/netprofm_test.exe"
fi

work_dir=$(mktemp -d /tmp/wine-netprofm-policy-XXXXXX)
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

cc -shared -fPIC -Wall -Wextra -o "$work_dir/loader.so" "$source_dir/topology-backend.c" -ldl
cc -shared -fPIC -Wall -Wl,-z,defs -DNETPROFM_UNSUPPORTED_BACKEND -D__WINESRC__ -DWINE_UNIX_LIB \
    -I"$build_dir/include" -I"$source_dir/../../../include" \
    -o "$work_dir/backend.so" "$source_dir/topology-backend.c"

ip link set lo up
ip link add nlm0 type dummy
sysctl -qw net.ipv6.conf.nlm0.keep_addr_on_down=1
ip addr add 192.0.2.2/24 dev nlm0
ip -6 addr add 2001:db8::2/64 dev nlm0 nodad
mkdir "$prefix"
if [ -n "$run_uid" ]; then chown "$run_uid:$run_gid" "$prefix"; fi

for test_exe in $test_exes; do
    test -f "$test_exe"
    for state in up down; do
        ip link set nlm0 "$state"
        if [ "$state" = up ]; then ip route add 198.51.100.0/24 via 192.0.2.1 dev nlm0; fi
        for policy in snapshot dynamic; do
            echo "topology policy: $test_exe $policy-$state"
            backend=$build_dir/dlls/netprofm/netprofm.so
            if [ "$policy" = snapshot ]; then backend=$work_dir/backend.so; fi
            result=0
            run_as_user env WINEPREFIX="$prefix" WINETEST_NETPROFM_TOPOLOGY_POLICY="$policy-$state" \
                WINETEST_NETPROFM_BACKEND="$backend" LD_PRELOAD="$work_dir/loader.so" \
                DBUS_SYSTEM_BUS_ADDRESS="unix:path=$work_dir/no-bus" \
                "$build_dir/wine" "$test_exe" list >"$work_dir/test.log" 2>&1 || result=$?
            cat "$work_dir/test.log"
            run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -w
            [ "$result" -eq 0 ] || exit "$result"
        done
    done
done
