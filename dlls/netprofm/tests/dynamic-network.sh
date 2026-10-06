#!/bin/sh
set -eu

if [ "${1-}" != "--inside" ]; then
    build_dir=${1:?usage: $0 BUILD_DIR [TEST_EXE]}
    test_exe=${2-}
    if [ -n "$test_exe" ] && [ ! -f "$test_exe" ]; then
        echo "test executable not found: $test_exe" >&2
        exit 1
    fi
    if unshare --user --map-root-user --net true 2>/dev/null; then
        exec unshare --user --map-root-user --net "$0" --inside "$build_dir" "" "" "$test_exe"
    fi
    if command -v sudo >/dev/null && sudo -n true 2>/dev/null; then
        exec sudo unshare --net "$0" --inside "$build_dir" "$(id -u)" "$(id -g)" "$test_exe"
    fi
    echo "unprivileged user/network namespaces are unavailable" >&2
    exit 1
fi

build_dir=$2
run_uid=${3-}
run_gid=${4-}
marker_dir=/tmp/wine-netprofm-dynamic
prefix=/tmp/wine-netprofm-prefix
log=/tmp/wine-netprofm-dynamic.log
test_dir=$build_dir/dlls/netprofm/tests
test_exe=

for candidate in "${5-}" "$test_dir/netprofm_test.exe.so" "$test_dir/netprofm_test.exe" \
        "$test_dir/x86_64-windows/netprofm_test.exe" "$test_dir"/*-windows/netprofm_test.exe
do
    if [ -f "$candidate" ]; then
        test_exe=$candidate
        break
    fi
done
if [ -z "$test_exe" ]; then
    echo "could not find the netprofm test executable under $test_dir" >&2
    exit 1
fi

rm -rf "$marker_dir" "$prefix" "$log"
mkdir -p "$marker_dir" "$prefix"
if [ -n "$run_uid" ]; then
    chmod 777 "$marker_dir"
    chown "$run_uid:$run_gid" "$prefix"
fi

ip link set lo up
ip link add nlm0 type veth peer name nlm1
ip addr add 192.0.2.2/24 dev nlm0
ip -6 addr add 2001:db8::2/64 dev nlm0 nodad
ip link set nlm1 up
ip link set nlm0 up
ip route add default via 192.0.2.1 dev nlm0

if [ -n "$run_uid" ]; then
    run_home=$(getent passwd "$run_uid" | cut -d: -f6)
    setpriv --reuid="$run_uid" --regid="$run_gid" --init-groups env HOME="$run_home" \
        WINETEST_NETPROFM_DYNAMIC_DIR='Z:\tmp\wine-netprofm-dynamic' WINEPREFIX="$prefix" \
        "$build_dir/wine" "$test_exe" list >"$log" 2>&1 &
else
    WINETEST_NETPROFM_DYNAMIC_DIR='Z:\tmp\wine-netprofm-dynamic' WINEPREFIX="$prefix" \
        "$build_dir/wine" "$test_exe" list >"$log" 2>&1 &
fi
test_pid=$!

wait_marker()
{
    marker=$1
    i=0
    while [ ! -e "$marker_dir/$marker" ]; do
        if ! kill -0 "$test_pid" 2>/dev/null; then
            cat "$log"
            wait "$test_pid"
        fi
        i=$((i + 1))
        if [ "$i" -ge 300 ]; then
            echo "timed out waiting for $marker" >&2
            cat "$log"
            exit 1
        fi
        sleep 0.1
    done
}

wait_marker initial_no_ipv6_route
ip -6 route add default via 2001:db8::1 dev nlm0 onlink
touch "$marker_dir/initial_ipv6_route_added"
wait_marker ready
ip route add 198.51.100.0/24 via 192.0.2.1 dev nlm0
ip route del default
wait_marker route_removed
ip route add blackhole default
touch "$marker_dir/ipv4_unusable_route_added"
wait_marker ipv4_unusable_route_checked
ip route del blackhole default
ip route add default dev nlm0
wait_marker route_onlink_restored
ip route del default dev nlm0
wait_marker route_onlink_removed
ip route add default via 192.0.2.1 dev nlm0
wait_marker route_restored
ip -6 route del default
wait_marker ipv6_route_removed
ip -6 route add blackhole default
touch "$marker_dir/ipv6_unusable_route_added"
wait_marker ipv6_unusable_route_checked
ip -6 route del blackhole default
ip -6 route add default dev nlm0
wait_marker ipv6_route_onlink_restored
ip -6 route del default dev nlm0
wait_marker ipv6_route_onlink_removed
ip -6 route add default via 2001:db8::1 dev nlm0 onlink
wait_marker ipv6_route_restored
ip addr del 192.0.2.2/24 dev nlm0
wait_marker address_removed
ip addr add 192.0.2.2/24 dev nlm0
ip route add default via 192.0.2.1 dev nlm0
wait_marker address_restored
ip link set nlm1 down
wait_marker carrier_down
ip link set nlm1 up

wait_marker add_adapter
ip link add nlm2 type dummy
ip addr add 203.0.113.2/24 dev nlm2
ip -6 addr add 2001:db8:2::2/64 dev nlm2 nodad
ip link set nlm2 up
ip route add 198.51.100.0/24 via 203.0.113.1 dev nlm2 metric 100
touch "$marker_dir/adapter_added"

if ! wait "$test_pid"; then
    cat "$log"
    exit 1
fi
cat "$log"
