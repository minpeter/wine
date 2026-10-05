#!/bin/sh
set -eu

if [ "${1-}" != "--inside" ]; then
    build_dir=${1:?usage: $0 BUILD_DIR}
    build_dir=$(CDPATH= cd -- "$build_dir" && pwd)
    test_dir=$build_dir/dlls/netprofm/tests
    test_exes=

    if [ -f "$test_dir/x86_64-windows/netprofm_test.exe" ] &&
       [ -f "$test_dir/i386-windows/netprofm_test.exe" ]; then
        test_exes="$test_dir/x86_64-windows/netprofm_test.exe $test_dir/i386-windows/netprofm_test.exe"
    else
        for candidate in "$test_dir/netprofm_test.exe.so" "$test_dir/netprofm_test.exe" \
                "$test_dir/x86_64-windows/netprofm_test.exe" "$test_dir"/*-windows/netprofm_test.exe
        do
            if [ -f "$candidate" ]; then
                test_exes=$candidate
                break
            fi
        done
    fi
    if [ -z "$test_exes" ]; then
        echo "could not find the netprofm test executable under $test_dir" >&2
        exit 1
    fi

    if unshare --user --map-root-user --net true 2>/dev/null; then
        for test_exe in $test_exes; do
            label=$(basename "$(dirname "$test_exe")")
            unshare --user --map-root-user --net "$0" --inside "$build_dir" "$test_exe" "$label"
        done
        exit
    fi
    if command -v sudo >/dev/null && sudo -n true 2>/dev/null; then
        for test_exe in $test_exes; do
            label=$(basename "$(dirname "$test_exe")")
            sudo unshare --net "$0" --inside "$build_dir" "$test_exe" "$label" "$(id -u)" "$(id -g)"
        done
        exit
    fi
    echo "unprivileged user/network namespaces are unavailable" >&2
    exit 1
fi

build_dir=$2
test_exe=$3
label=$4
run_uid=${5-}
run_gid=${6-}
source_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
marker_dir=/tmp/wine-netprofm-reachability-$label
prefix=/tmp/wine-netprofm-reachability-prefix-$label
log=/tmp/wine-netprofm-reachability-$label.log
bus_info=/tmp/wine-netprofm-reachability-bus-$label
bus_socket=/tmp/wine-netprofm-reachability-bus-$label.sock
marker_win=Z:\\tmp\\wine-netprofm-reachability-$label

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
    if [ -s "$marker_dir/activated" ]; then
        kill "$(cat "$marker_dir/activated")" 2>/dev/null || true
    fi
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

# Prefix bootstrap can exceed the marker deadline, particularly under WoW64.
# Finish it before starting the activation bus and timed test exchanges.
run_as_user env WINEPREFIX="$prefix" DBUS_SYSTEM_BUS_ADDRESS="unix:path=$marker_dir/no-bus" \
    "$build_dir/wine" wineboot --init >"$log" 2>&1
run_as_user env WINEPREFIX="$prefix" "$build_dir/server/wineserver" -w

bus_address="unix:path=$bus_socket"
# Install an activatable but initially stopped service on a private bus. The
# wrapper records process startup, even if name acquisition or GetAll fails.
mkdir "$marker_dir/services"
cat >"$marker_dir/activate" <<EOF
#!/bin/sh
echo \$\$ >"$marker_dir/activated"
export DBUS_SYSTEM_BUS_ADDRESS="$bus_address"
exec /usr/bin/python3 "$source_dir/fake-networkmanager.py" \\
    "$marker_dir/activation-command" "$marker_dir/activation-ready" FULL 1 1
EOF
chmod 755 "$marker_dir/activate"
cat >"$marker_dir/services/org.freedesktop.NetworkManager.service" <<EOF
[D-BUS Service]
Name=org.freedesktop.NetworkManager
Exec=$marker_dir/activate
EOF
cat >"$marker_dir/bus.conf" <<EOF
<busconfig>
  <type>session</type>
  <listen>$bus_address</listen>
  <servicedir>$marker_dir/services</servicedir>
  <policy context="default">
    <allow send_destination="*"/>
    <allow receive_sender="*"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
start_bus()
{
    rm -f "$bus_info" "$bus_socket"
    run_as_user dbus-daemon --config-file="$marker_dir/bus.conf" --fork \
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

# Positive control: the same ordinary GetAll request must activate the service.
run_as_user dbus-send --bus="$bus_address" --print-reply \
    --dest=org.freedesktop.NetworkManager /org/freedesktop/NetworkManager \
    org.freedesktop.DBus.Properties.GetAll string:org.freedesktop.NetworkManager
wait_marker activation-ready
test -s "$marker_dir/activated"
kill "$(cat "$marker_dir/activated")"
# Restart the bus to guarantee the activatable service has no owner.
kill "$bus_pid"
bus_pid=
start_bus
rm -f "$marker_dir/activated" "$marker_dir/activation-ready"
echo "activation control passed; starting NLM with NetworkManager stopped"
if [ -n "$run_uid" ]; then
    run_home=$(getent passwd "$run_uid" | cut -d: -f6)
    setpriv --reuid="$run_uid" --regid="$run_gid" --init-groups env HOME="$run_home" \
        DBUS_SYSTEM_BUS_ADDRESS="$bus_address" \
        WINETEST_NETPROFM_REACHABILITY_DIR="$marker_win" WINEPREFIX="$prefix" \
        "$build_dir/wine" "$test_exe" list >"$log" 2>&1 &
else
    env DBUS_SYSTEM_BUS_ADDRESS="$bus_address" \
        WINETEST_NETPROFM_REACHABILITY_DIR="$marker_win" WINEPREFIX="$prefix" \
        "$build_dir/wine" "$test_exe" list >"$log" 2>&1 &
fi
test_pid=$!

wait_marker passive
test ! -e "$marker_dir/activated"
start_service service-ready FULL
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
sleep 2
test ! -e "$marker_dir/activated"
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
sleep 2
test ! -e "$marker_dir/activated"
start_service bus-service-restarted PORTAL
wait_marker bus_restarted
set_state 7 FULL 1 1

if ! wait "$test_pid"; then
    cat "$log"
    exit 1
fi
test_pid=
test ! -e "$marker_dir/activated"
echo "passive activation checks passed (startup, owner loss, reconnect)"
cat "$log"
