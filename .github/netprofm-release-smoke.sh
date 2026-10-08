#!/bin/bash
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
# Runtime checks from the pinned Soda recipe; use only a disposable prefix.
set -euo pipefail
runner=$(realpath "${1:?usage: $0 RUNNER NEW_LOG_DIR}")
mkdir "${2:?}"
logs=$(realpath "$2")
packaging=$(realpath "$(dirname "$0")/..")
scratch=$(mktemp -d)
export WINEDEBUG=-all TZ=America/Denver
cleanup()
{
    "$runner/bin/wineserver" -k 2>/dev/null || true
    "$runner/bin/wineserver" -w 2>/dev/null || true
    rm -rf "$scratch"
}
trap cleanup EXIT
for architecture in x86_64 i686; do
    export WINEARCH=win64
    [[ $architecture != i686 ]] || export WINEARCH=win32
    export WINEPREFIX=$scratch/prefix-$WINEARCH
    compiler=$architecture-w64-mingw32-gcc
    "$compiler" "$packaging/.github/soda-office-winrt-smoke.c" \
        -O2 -Wall -Wextra -Werror -lole32 -lruntimeobject -o "$scratch/office.exe"
    "$compiler" "$packaging/.github/soda-ldap-smoke.c" \
        -O2 -Wall -Wextra -Werror -o "$scratch/ldap.exe"
    "$compiler" "$packaging/.github/soda-icu-smoke.c" \
        -O2 -Wall -Wextra -Werror -o "$scratch/icu.exe"
    "$compiler" "$packaging/.github/soda-file-security-smoke.c" \
        -O2 -Wall -Wextra -Werror -ladvapi32 -o "$scratch/security.exe"
    xvfb-run -a "$runner/bin/wineboot" -u > "$logs/$WINEARCH-boot.log" 2>&1
    xvfb-run -a "$runner/bin/wine" "$scratch/office.exe" | tr -d '\r' > "$logs/$WINEARCH-office.log"
    for class in core_application message_websocket protection_policy retail_info \
            web_authentication_manager composition_effect_source_parameter; do
        grep -Fx "$class:00000000" "$logs/$WINEARCH-office.log"
    done
    xvfb-run -a "$runner/bin/wine" reg add \
        'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\ldap.exe' \
        /v GlobalFlag /t REG_DWORD /d 112 /f
    xvfb-run -a "$runner/bin/wine" "$scratch/ldap.exe" | tr -d '\r' > "$logs/$WINEARCH-ldap.log"
    grep -Fx 'ldap_init_checks:1024 failures:0' "$logs/$WINEARCH-ldap.log"
    xvfb-run -a "$runner/bin/wine" "$scratch/icu.exe" | tr -d '\r' > "$logs/$WINEARCH-icu.log"
    grep -F 'ICU 72.1 timezone=America/Denver' "$logs/$WINEARCH-icu.log"
    xvfb-run -a "$runner/bin/wine" "$scratch/security.exe" | tr -d '\r' > "$logs/$WINEARCH-security.log"
    grep -Fx 'reopen:ok' "$logs/$WINEARCH-security.log"
    "$runner/bin/wineserver" -w
    rm -rf "$WINEPREFIX"
done
echo 'Soda Office/LDAP/ICU/file-security smokes passed for x64 and Win32.'
