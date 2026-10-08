# Soda 11.0-10: reviewed dynamic NLM connectivity

AI-assisted implementation and port using Amp, authored by Woonggi Min.
Original copyright/LGPL-2.1-or-later notices and prerequisite attribution to
Hans Leidekker and Paul Gofman are retained. Derived from the reviewed
minpeter/wine series `455e3509b98a6919fd4ad1def4803e08c41c03b2` through
`b82c41eca5ffd18a198afe008d2dc9f6fc53d453`. No upstream approval is implied.

## Stable recipe, not the newer Experimental stack

This branch starts at the exact official **soda-11.0-10** tag:
`75ceacbc281484a46704131ed3c338fbd08d3d3c`. Its original 21 user patches,
import scripts, workflow and configuration are unchanged. The only additions
are the reviewed 29-file NLM/NSI patch, focused-check wrapper and this document.
Later Experimental Office/Eagle/identity/window/input changes are not imported.
Original stable fixes remain, including media, OpenXR, split-prefix layout,
username preservation and adaptive launch behavior already present in 11.0-10.

| Input | Exact revision |
| --- | --- |
| Soda 11.0-10 recipe | `75ceacbc281484a46704131ed3c338fbd08d3d3c` |
| Valve Wine | `0509a0b64a4852741364c95bcdc67ae8f74ef11d` |
| Wine-TKG | `04ddf86c96817d2f8bb45aefc04c9ad3871ca740` |
| Build tools | `fcba104217c6daddddebe69d2dc2e39bc9025848` |
| Proton | `b7a763327f0cb76e9e0bd53dc4de95dee0d4c3b7` |
| FEX | `1cc4b93e7a71c883ec021b71359f136394dc1f3c` |
| OpenXR SDK | `8899a91c17ce9618f565f42408b47db1d6e9ccc7` |

Valve tag:
`experimental-wine-bleeding-edge-11.0-428201-20260904-pdf1645-w0509a0-dd7ac25-v35bdee`.
MS-ICU and Proton wineopenxr are imported exactly as specified by the stable
recipe. Mono is 10.4.1, with the original recipe hash.

## Policy and integration

The patch preserves Valve netprofm `--prefer-native`, independent ICMP listener
and device behavior. Required reviewed IPv6/loopback prerequisites are included
from `ea887800fb7b`, `1d197dfb5568`, `9c0bf5fda079` and `91b081763ce7`.
There were no additional stable-port application conflicts or new production
adaptations. Do not replace Valve NSI wholesale.

Linux topology uses NSI notifications, operational carrier and usable default
routes, including on-link routes and excluding rejected/down routes. Event sinks
use apartment-safe COM GIT dispatch and preserve object identity/lifetime.
Optional NetworkManager properties and authenticated owner signals are passive:
no activation or connectivity probes. Missing D-Bus falls back to topology;
monitor startup failure retains snapshot behavior; fatal runtime failure drains
subscriptions and downgrades. Terminal NSI failures remain sticky until device-host
restart. Private strict IPv6 queries distinguish unavailable from empty without
changing the public success-empty contract. Provider reconnect is not whole-Wine
device-host/system-bus recovery proof.

## Current verification status

**Full stable build and runtime QA are pending.** Do not interpret the previous
11.0-28 variant's passes or the official 11.0-10 runner's QA as candidate results.
This source branch is published independently of binary delivery.

Executed for this variant:

* All original 21 stable patches retained byte-for-byte; ordered 22-patch stack
  (including NLM) applies cleanly to the pinned imports without conflicts.
* Replay source tree: `d894d9cf435e44743fec1cb361caa908a40a101c`.
* Actual pinned TKG `_init`, `_prepare` and `_polish` completed. All 29 NLM/NSI
  files match the reviewed port byte-for-byte after recipe preparation.
* NLM patch payload unchanged; wrapper shell syntax and added-source whitespace
  checks passed. Blob-level whitespace checking flags required unified-patch
  context/footer bytes, which are intentionally retained. Imported wineopenxr
  contains inherited generator trailing whitespace, not introduced by this branch.

A separate **non-PGO** traditional split x64/i386 runner build is in progress
using the stable TKG configuration and a retained isolated Ubuntu24.04 dependency
overlay, not host package changes. Native/CROSS optimization flags, Wayland,
Vulkan, OpenGL, FFmpeg and GStreamer are retained; i386 wineopenxr is disabled
as in the original recipe. Tests are enabled and ccache is disabled for validation.
The official stable release used measured PGO; this candidate does not claim that
PGO training/gating was reproduced. The overlay is not a pinned OCI image: retain
actual package-version and apt-source manifests alongside any private build.

Planned candidate checks: ordinary NLM/NSI/IPHelper; topology/carrier/on-link;
strict IPv6 unavailable; monitor/snapshot faults; malformed/authenticated D-Bus;
real NSI faults; both architectures' STA and passive NM reconnect; no-D-Bus.
ARM64 compile/runtime and physical network outage/suspend coverage are not claimed.
Application GUI/media behavior requires separate disposable-prefix workstation QA.

## Reproduce focused checks

Follow the **11.0-10** workflow's pinned fetch/import/TKG preparation sequence,
including its original stable patches plus the NLM patch. Generate Valve Vulkan,
server/request/spec outputs and configure files as the pinned TKG recipe does.
Configure/build both traditional Unix architectures with tests enabled, then run:

```sh
bash .github/check-netprofm.sh /absolute/WINE_SOURCE /absolute/WINE_BUILD /absolute/NEW_LOG_DIR
```

The wrapper uses sudo mount/network namespaces, `ip`, `setpriv`, `sysctl`, a C
compiler and configured Wine tools. D-Bus tests need dbus-daemon, development
headers/pkg-config and Python dbus/GLib. It isolates fixture temporary paths,
preserves `CC`/architecture selection and uses shared wine64 tools for split-i386.
Select each architecture's build directory separately. D-Bus suites are omitted
when its configured SONAME is absent; omission is not a D-Bus test pass.

No upstream workflow was modified or manually triggered. Publish only this
non-trigger port branch, never `soda`: the inherited push workflow on `soda`
can build/publish releases. Private logs, fixtures, runner/runtime binaries,
workstation diagnostics, account data and backups are not included here.
