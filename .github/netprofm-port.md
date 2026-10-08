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

**The paired-module build and Linux overlay regressions passed. Workstation QA
reports candidate Bottles, clean/upgraded-prefix NLM and GUI/input checks passed.**
Previous 11.0-28 results and official 11.0-10 baseline results are not substituted
for these candidate checks. Global candidate installation is still separate;
no switch of the live application is claimed.

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

### Delivery: official PGO runtime plus only the changed network owners

The candidate uses the official **soda-11.0-10-x86_64** archive, 174028776 bytes,
SHA256 `14633fabd180d308d2c1ebcb84a16f36d0484dded5809c64f8a7c93e654d7296`.
Extract it into a distinct runner directory before applying the overlay.
Only both architectures' `netprofm.dll`, `nsiproxy.sys`, `nsiproxy.so` are replaced;
both `netprofm.so` files are added (eight files total). The other 4,479 regular
files match the fresh official archive byte-for-byte, including all unchanged
input/window/graphics/media components and their official measured PGO output.
Do not overwrite the official or another installed runner.

The eight modules and regression PEs were freshly compiled in dedicated
traditional x64/i386 trees using the pinned TKG configure functions, stable
optimization flags, tests enabled and ccache disabled, in an isolated Ubuntu24.04
dependency environment. Full Valve ancestry was recovered before these builds:
shallow history can incorrectly select TKG's legacy CROSS flag fallback.
No PGO training/gating is claimed for the two changed module owners. Public NSI
layouts and interface signatures remain unchanged; the strict IPv6 table ID is
private and the unchanged NLM interfaces now have an embedded/registerable typelib.
All 61 netprofm and 39 nsiproxy PE import symbols per architecture exist in the
official runtime, and original netprofm exports are retained. All four Unix
libraries resolve against the same-architecture official ntdll without missing
symbols or symbol-version errors. Executed overlay tests additionally validate
the paired Unix-call ABI and apartment dispatch; matching source pins alone is
not the compatibility proof.

The separate full reference x64 build completed with an earlier shallow-history
legacy CROSS flag fallback. Its modules are not delivered. The following isolated
i386 development-package switch failed in package-state backup handling; full
reference i386 build/install did not complete. This is not a clean full release
rebuild or a full reference-build pass. Partial-module configurations' missing
optional unrelated development dependencies do not strip any feature from the
official runtime retained by the overlay. The dependency environment is not a
pinned OCI image: retain package-version and apt-source manifests with private
build evidence, and do not substitute new-WoW64 for traditional split i386.

### Executed complete-overlay runtime checks

Every regression PE ran through the complete official runner with the eight-file
overlay, explicitly selected loader/server/DLL paths and disposable prefixes;
partial build-tree loaders were not used for runtime closure. Both architectures
passed the following, with zero failures:

| Check | x64 / i386 assertions per architecture | Skips |
| --- | --- | --- |
| Ordinary NLM | 87 | 0 |
| Ordinary NSI | 1,751 | 0 |
| Ordinary IPHelper | 1,070 | 1 each: global IPv6 unreachable |
| Dynamic topology, carrier, default/on-link/IPv6 routes and STA callbacks | 200 | 0 |
| Snapshot/dynamic topology policies | 110 + 110 + 103 + 103 | 0 |
| Strict IPv6 unavailable versus empty | 110 + 111 | 0 |
| Monitor faults / malformed-authenticated D-Bus | 826 + 110; 254 D-Bus fixture checks | 0 |
| NSI device / fatal, retained and control interleavings | 1,000 + 148 + 148 + 148 + 149 | 0 |
| Passive fake-NM startup, owner loss and reconnect / STA dispatch | 136 | 0 |

Inherited Wine todos remain visible, not converted to passes. Separate no-D-Bus
Unix builds (SONAME definition absent) passed all four snapshot/dynamic topology
policy cases on both architectures; those test-only libraries are not delivered.
Namespace topology and passive fake-NM fixtures do not prove physical network
outage/suspend or whole-Wine device-host/system-bus recovery.

### Separate workstation QA (reported by the workstation QA owner)

The actual candidate was assembled from the freshly downloaded official archive
and the same hash-checked eight modules. All 4,479 unaffected files matched the
official archive; exactly six replacements and two additions matched the handoff.
The following checks passed for both x64 and traditional i386:

* Clean disposable-prefix NLM: 125 assertions, zero failures/skips and eight
  inherited todos per architecture. Counts depend on the enumerated host topology.
* A genuinely original official-runner prefix had no NLM typelib. Normal candidate
  `wineboot -u` registered typelib version 1.0 and the universal marshaler in both
  registry views; upgraded-prefix NLM then passed 125 assertions with the same
  zero failures/skips and inherited todos. No live account prefix was copied.
* Native Bottles namespace STA/topology: 200 assertions; private passive fake-NM
  and reconnect: 136 assertions. Zero failures/skips/flaky, with twelve inherited
  todos total per architecture. Only the private launch adapter's hardcoded win32
  selection needed correction to use the requested `WINEARCH`; no Wine source fix.
* Private native edit-control input fixture: seven real X11 wheel events, normal
  WinAPI F6 minimize/restore, focus recovery, retained scroll position and subsequent
  wheel movement to offset 21. Four architecture/state screenshots were inspected
  by the workstation QA owner. This is native Wine input QA, not Qt/chat scrolling.

The candidate also rendered an account-free application's empty Qt login surface
and normal WinAPI minimize/restore state; screenshots were inspected by the
workstation QA owner. Candidate loader mappings and host Mesa GL were verified;
no account/chat contents were captured and no OpenGL error was reported. This is
rendering/restore coverage, not authenticated login or application media playback.
The live application, its existing runner, settings and data remained untouched
during this disposable candidate QA. Workstation logs/screenshots remain private.

ARM64 compilation/runtime, physical outage/suspend, authenticated application
login, application scrolling and application media playback are not claimed.
Keep graphics ABI overrides out of the global Bottles/Python environment and
preserve existing user data/settings. Installation or switching an existing
live bottle is a separate operation, not implied by these QA passes.

## Reproduce focused checks

Follow the **11.0-10** workflow's pinned fetch/import/TKG preparation sequence,
including its original stable patches plus the NLM patch. Generate Valve Vulkan,
server/request/spec outputs and configure files as the pinned TKG recipe does.
Configure/build both traditional Unix architectures with tests enabled. For a
complete build tree, run:

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
