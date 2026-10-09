#!/bin/bash
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
# Invalid paths must fail before creating logs; no Wine build is required.
set -euo pipefail
wrapper=$(realpath "$(dirname "$0")/../check-netprofm.sh")
outside=$(mktemp -d /var/tmp/soda-nlm-paths.XXXXXX)
inside=$(mktemp -d /tmp/soda-nlm-paths.XXXXXX)
trap 'rm -rf -- "$outside" "$inside"' EXIT
mkdir "$outside/source" "$outside/build" "$inside/source" "$inside/build"

reject_before_logs()
{
    local script=$1 source=$2 build=$3 logs=$4
    if bash "$script" "$source" "$build" "$logs" > "$outside/result.log" 2>&1; then
        echo "FAIL: accepted an input hidden by the private /tmp mount" >&2
        exit 1
    fi
    if [[ -e $logs ]]; then
        echo "FAIL: created logs before rejecting hidden inputs" >&2
        cat "$outside/result.log" >&2
        exit 1
    fi
}

reject_before_logs "$wrapper" "$inside/source" "$outside/build" "$outside/source-logs"
reject_before_logs "$wrapper" "$outside/source" "$inside/build" "$outside/build-logs"
reject_before_logs "$wrapper" "$outside/source" "$outside/build" "$inside/new-logs"
ln -s "$inside/source" "$outside/source-link"
reject_before_logs "$wrapper" "$outside/source-link" "$outside/build" "$outside/symlink-logs"
cp "$wrapper" "$inside/check-netprofm.sh"
reject_before_logs "$inside/check-netprofm.sh" "$outside/source" "$outside/build" "$outside/wrapper-logs"
echo 'PASS: source/build/log/symlink/wrapper paths under /tmp rejected before log creation.'
