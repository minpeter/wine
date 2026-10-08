#!/bin/bash
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
# Launch only Wine/the test child with these paths, never the Bottles GUI itself.
set -euo pipefail
bundle=$(realpath "$(dirname "$0")")
architecture=${1:?usage: run-with-runtime.sh i386|x86_64 COMMAND [ARGUMENTS...]}
shift
case $architecture in
    i386) selected=i386-linux-gnu; other=x86_64-linux-gnu;;
    x86_64) selected=x86_64-linux-gnu; other=i386-linux-gnu;;
    *) echo 'Expected i386 or x86_64 (architecture of the Windows media application).' >&2; exit 2;;
esac
export LD_LIBRARY_PATH="$bundle/lib/$selected:$bundle/lib/$other${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export GST_PLUGIN_SYSTEM_PATH_1_0="$bundle/lib/$selected/gstreamer-1.0"
export GST_PLUGIN_PATH_1_0="$GST_PLUGIN_SYSTEM_PATH_1_0"
export GST_PLUGIN_SCANNER_1_0="$bundle/libexec/$selected/gst-plugin-scanner"
cache=${SODA_RUNTIME_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/soda-nlm-ubuntu24-runtime}
mkdir -p "$cache/$selected"
export GST_REGISTRY_1_0="$cache/$selected/registry.bin"
export CAMLIBS=$(find "$bundle/lib/$selected/libgphoto2" -mindepth 1 -maxdepth 1 -type d -print -quit)
export IOLIBS=$(find "$bundle/lib/$selected/libgphoto2_port" -mindepth 1 -maxdepth 1 -type d -print -quit)
exec "${@:?missing command}"
