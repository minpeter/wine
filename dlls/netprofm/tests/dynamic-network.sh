#!/bin/sh
set -eu

if [ "${1-}" != "--inside" ]; then
    build_dir=${1:?usage: $0 BUILD_DIR}
    if unshare --user --map-root-user --net true 2>/dev/null; then
        exec unshare --user --map-root-user --net "$0" --inside "$build_dir"
    fi
    if command -v sudo >/dev/null && sudo -n true 2>/dev/null; then
        exec sudo unshare --net "$0" --inside "$build_dir" "$(id -u)" "$(id -g)"
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

rm -rf "$marker_dir" "$prefix" "$log"
mkdir -p "$marker_dir" "$prefix"
if [ -n "$run_uid" ]; then
    chmod 777 "$marker_dir"
    chown "$run_uid:$run_gid" "$prefix"
fi

ip link set lo up
ip link add nlm0 type dummy
ip addr add 192.0.2.2/24 dev nlm0
ip -6 addr add 2001:db8::2/64 dev nlm0 nodad
ip link set nlm0 up
ip route add default via 192.0.2.1 dev nlm0

if [ -n "$run_uid" ]; then
    run_home=$(getent passwd "$run_uid" | cut -d: -f6)
    setpriv --reuid="$run_uid" --regid="$run_gid" --init-groups env HOME="$run_home" \
        WINETEST_NETPROFM_DYNAMIC_DIR='Z:\tmp\wine-netprofm-dynamic' WINEPREFIX="$prefix" \
        "$build_dir/wine" "$build_dir/dlls/netprofm/tests/netprofm_test.exe.so" list >"$log" 2>&1 &
else
    WINETEST_NETPROFM_DYNAMIC_DIR='Z:\tmp\wine-netprofm-dynamic' WINEPREFIX="$prefix" \
        "$build_dir/wine" "$build_dir/dlls/netprofm/tests/netprofm_test.exe.so" list >"$log" 2>&1 &
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
ip route del default
wait_marker route_removed
ip route add default via 192.0.2.1 dev nlm0
wait_marker route_restored
ip -6 route del default
wait_marker ipv6_route_removed
ip -6 route add default via 2001:db8::1 dev nlm0 onlink
wait_marker ipv6_route_restored
ip addr del 192.0.2.2/24 dev nlm0
wait_marker address_removed
ip addr add 192.0.2.2/24 dev nlm0
ip route add default via 192.0.2.1 dev nlm0
wait_marker address_restored
ip link set nlm0 down
wait_marker down
ip link set nlm0 up
sleep 0.5
ip -6 addr add 2001:db8::2/64 dev nlm0 nodad
ip route replace default via 192.0.2.1 dev nlm0
ip -6 route replace default via 2001:db8::1 dev nlm0 onlink

if ! wait "$test_pid"; then
    cat "$log"
    exit 1
fi
cat "$log"
