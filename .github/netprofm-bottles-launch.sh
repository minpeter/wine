#!/bin/bash
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
# Adapter for the unchanged topology/NM fixtures: only observer launches use
# Bottles; bootstrap and prefix-specific wineserver control use the same runner.
set -euo pipefail
: "${SODA_QA_ROOT:?}" "${SODA_QA_RUNNER:?}" "${SODA_QA_FLATPAK:?}"
export XDG_DATA_HOME=$SODA_QA_ROOT/data XDG_CONFIG_HOME=$SODA_QA_ROOT/config
export XDG_CACHE_HOME=$SODA_QA_ROOT/cache GSETTINGS_BACKEND=keyfile
name=SodaNLM-isolated
component=$(basename "$SODA_QA_RUNNER")
mode=$(basename "$0")
if [[ $mode != wine && $mode != wineserver ]]; then mode=${1:?}; shift; fi
run()
{
    local executable=$1
    shift
    if [[ $SODA_QA_FLATPAK == 1 ]]; then
        # Locate the existing user Flatpak installation with its original XDG
        # paths; only the application receives the disposable Bottles paths.
        env -u XDG_DATA_HOME -u XDG_CONFIG_HOME -u XDG_CACHE_HOME \
            flatpak run --nofilesystem=home --filesystem="$SODA_QA_ROOT" --filesystem=/tmp \
            --command=env com.usebottles.bottles \
            XDG_DATA_HOME="$XDG_DATA_HOME" XDG_CONFIG_HOME="$XDG_CONFIG_HOME" \
            XDG_CACHE_HOME="$XDG_CACHE_HOME" GSETTINGS_BACKEND=keyfile \
            GDK_BACKEND=x11 WINEPREFIX="${WINEPREFIX-}" WINEARCH=win32 \
            DBUS_SYSTEM_BUS_ADDRESS="${DBUS_SYSTEM_BUS_ADDRESS-}" "$executable" "$@"
    else
        "$executable" "$@"
    fi
}
case $mode in
    cli) run bottles-cli "$@"; exit;;
    gui) run bottles "$@"; exit;;
    settings) run gsettings "$@"; exit;;
    wineserver) run "$SODA_QA_RUNNER/bin/wineserver" "$@"; exit;;
    wine) ;;
    *) exit 2;;
esac

if [[ ${1-} != *.exe ]]; then run "$SODA_QA_RUNNER/bin/wine" "$@"; exit; fi
[[ $# == 2 && $2 == list && -n ${WINEPREFIX-} ]]
configuration=$XDG_DATA_HOME/bottles/bottles/$name/bottle.yml
mkdir -p "$(dirname "$configuration")"
python3 - "$configuration" "$name" "$component" "$WINEPREFIX" <<'PY'
import json
import os
import sys
path, name, runner, prefix = sys.argv[1:]
environment = {key: os.environ.get(key, "") for key in (
    "DBUS_SYSTEM_BUS_ADDRESS", "WINETEST_NETPROFM_DYNAMIC_DIR",
    "WINETEST_NETPROFM_REACHABILITY_DIR")}
environment["WINEDEBUG"] = "-all"
# JSON is YAML 1.2: use the standard library, not a host PyYAML dependency.
with open(path + ".tmp", "w") as file:
    json.dump({"Name": name, "Runner": runner, "Arch": "win32", "Windows": "win10",
               "Path": prefix, "Custom_Path": True, "Environment": "Custom",
               "Limit_System_Environment": False, "Environment_Variables": environment,
               "Parameters": {"renderer": "gl", "dxvk": False, "vkd3d": False,
                              "use_runtime": False, "use_steam_runtime": False,
                              "sync": "wine"}}, file)
os.replace(path + ".tmp", path)
PY
observer_log=$WINEPREFIX/sta-observer.log
windows_exe="Z:${1//\//\\}"
windows_log="Z:${observer_log//\//\\}"
if [[ ! -d $WINEPREFIX/drive_c ]]; then
    run "$SODA_QA_RUNNER/bin/wine" wineboot --init
fi
# Bottles assembles cmd arguments again: keep nested quoting/redirection in a
# Windows batch file, not a cmd /c command string that loses its quote boundary.
printf '@echo off\r\n"%s" list > "%s" 2>&1\r\nexit /b %%errorlevel%%\r\n' \
    "$windows_exe" "$windows_log" > "$WINEPREFIX/drive_c/sta-observer.cmd"
run bottles-cli run -b "$name" -e 'C:\windows\system32\cmd.exe' --args-replace \
    /c 'C:\sta-observer.cmd'
# Do not mistake the launcher's exit status for the observer's result.
for ((attempt=0; attempt<600; attempt++)); do
    if [[ -f $observer_log ]] && grep -q ' tests executed ' "$observer_log"; then
        cat "$observer_log"
        grep -Eq ' tests executed \([^)]*, 0 failures\), [0-9]+ skipped\.' "$observer_log"
        exit
    fi
    sleep 0.1
done
cat "$observer_log" 2>/dev/null || true
echo 'No final STA observer assertion summary; this is not a QA pass.' >&2
exit 1
