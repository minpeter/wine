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
source_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
marker_dir=/tmp/wine-netprofm-reachability
prefix=/tmp/wine-netprofm-reachability-prefix
log=/tmp/wine-netprofm-reachability.log
bus_info=/tmp/wine-netprofm-reachability-bus
bus_socket=/tmp/wine-netprofm-reachability-bus.sock
test_dir=$build_dir/dlls/netprofm/tests
test_exe=

for candidate in "$test_dir/netprofm_test.exe.so" "$test_dir/netprofm_test.exe" \
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

rm -rf "$marker_dir" "$prefix" "$log" "$bus_info" "$bus_socket"
mkdir -p "$marker_dir" "$prefix"
if [ -n "$run_uid" ]; then
    chmod 777 "$marker_dir"
    chown "$run_uid:$run_gid" "$prefix"
fi

ip link set lo up
ip link add nlm0 type dummy
ip addr add 192.0.2.2/24 dev nlm0
ip link set nlm0 up
ip route add default via 192.0.2.1 dev nlm0

service_pid=
test_pid=
bus_pid=
cleanup()
{
    [ -z "$test_pid" ] || kill "$test_pid" 2>/dev/null || true
    [ -z "$service_pid" ] || kill "$service_pid" 2>/dev/null || true
    [ -z "$bus_pid" ] || kill "$bus_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

run_as_user()
{
    if [ -n "$run_uid" ]; then
        run_home=$(getent passwd "$run_uid" | cut -d: -f6)
        setpriv --reuid="$run_uid" --regid="$run_gid" --init-groups env HOME="$run_home" "$@"
    else
        "$@"
    fi
}

bus_address="unix:path=$bus_socket"
start_bus()
{
    rm -f "$bus_info" "$bus_socket"
    run_as_user dbus-daemon --session --address="$bus_address" --fork \
        --print-address=1 --print-pid=1 >"$bus_info"
    bus_pid=$(sed -n '2p' "$bus_info")
}

start_bus

start_service()
{
    ready=$1
    connectivity=$2
    rm -f "$marker_dir/$ready"
    if [ -n "$run_uid" ]; then
        run_home=$(getent passwd "$run_uid" | cut -d: -f6)
        setpriv --reuid="$run_uid" --regid="$run_gid" --init-groups env HOME="$run_home" \
            DBUS_SYSTEM_BUS_ADDRESS="$bus_address" /usr/bin/python3 \
            "$source_dir/fake-networkmanager.py" "$marker_dir/command" "$marker_dir/$ready" \
            "$connectivity" 1 1 &
    else
        env DBUS_SYSTEM_BUS_ADDRESS="$bus_address" /usr/bin/python3 \
            "$source_dir/fake-networkmanager.py" "$marker_dir/command" "$marker_dir/$ready" \
            "$connectivity" 1 1 &
    fi
    service_pid=$!
    wait_marker "$ready"
}

wait_marker()
{
    marker=$1
    i=0
    while [ ! -e "$marker_dir/$marker" ]; do
        if [ -n "$test_pid" ] && ! kill -0 "$test_pid" 2>/dev/null; then
            cat "$log"
            wait "$test_pid"
        fi
        i=$((i + 1))
        if [ "$i" -ge 300 ]; then
            echo "timed out waiting for $marker" >&2
            cat "$log" 2>/dev/null || true
            exit 1
        fi
        sleep 0.1
    done
}

set_state()
{
    serial=$1
    state=$2
    available=$3
    enabled=$4
    printf '%s %s %s %s\n' "$serial" "$state" "$available" "$enabled" >"$marker_dir/command.tmp"
    mv "$marker_dir/command.tmp" "$marker_dir/command"
    wait_marker "ack-$serial"
}

start_service service-ready FULL
if [ -n "$run_uid" ]; then
    run_home=$(getent passwd "$run_uid" | cut -d: -f6)
    setpriv --reuid="$run_uid" --regid="$run_gid" --init-groups env HOME="$run_home" \
        DBUS_SYSTEM_BUS_ADDRESS="$bus_address" \
        WINETEST_NETPROFM_REACHABILITY_DIR='Z:\tmp\wine-netprofm-reachability' WINEPREFIX="$prefix" \
        "$build_dir/wine" "$test_exe" list >"$log" 2>&1 &
else
    env DBUS_SYSTEM_BUS_ADDRESS="$bus_address" \
        WINETEST_NETPROFM_REACHABILITY_DIR='Z:\tmp\wine-netprofm-reachability' WINEPREFIX="$prefix" \
        "$build_dir/wine" "$test_exe" list >"$log" 2>&1 &
fi
test_pid=$!

wait_marker ready
set_state 1 LIMITED 1 1
wait_marker limited
set_state 2 UNKNOWN 1 1
wait_marker unknown
set_state 3 NONE 1 0
touch "$marker_dir/disabled"
wait_marker disabled_checked
set_state 4 NONE 1 1
wait_marker enabled
kill "$service_pid"
wait "$service_pid" || true
service_pid=
wait_marker stopped
rm -f "$marker_dir/command"
start_service service-restarted PORTAL
wait_marker restarted
set_state 5 FULL 1 1
wait_marker full
set_state 6 PORTAL 1 1
wait_marker bus_portal
kill "$bus_pid"
wait "$bus_pid" 2>/dev/null || true
bus_pid=
wait_marker bus_stopped
if kill -0 "$service_pid" 2>/dev/null; then kill "$service_pid"; fi
wait "$service_pid" 2>/dev/null || true
service_pid=
rm -f "$marker_dir/command"
start_bus
start_service bus-service-restarted PORTAL
wait_marker bus_restarted
set_state 7 FULL 1 1

if ! wait "$test_pid"; then
    cat "$log"
    exit 1
fi
test_pid=
cat "$log"
