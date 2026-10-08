# Dynamic NLM connectivity port for Soda

AI-assisted implementation and port using Amp, authored by Woonggi Min. Derived
from the reviewed minpeter/wine series
`455e3509b98a6919fd4ad1def4803e08c41c03b2` through
`b82c41eca5ffd18a198afe008d2dc9f6fc53d453`. Original copyright and
LGPL-2.1-or-later notices are retained. Includes prerequisite IPv6/loopback work
by Hans Leidekker and Paul Gofman. No upstream acceptance is implied.

## Exact inputs

The target is the **soda recipe**, not the repository's master Wine tree.

| Input | Revision |
| --- | --- |
| Soda | `b9e41a2512f69c895d6a44c6a9d64fb1cfbaeb97` |
| Valve Wine | `0509a0b64a4852741364c95bcdc67ae8f74ef11d` |
| Wine-TKG | `04ddf86c96817d2f8bb45aefc04c9ad3871ca740` |
| Build tools | `fcba104217c6daddddebe69d2dc2e39bc9025848` |
| Proton | `b7a763327f0cb76e9e0bd53dc4de95dee0d4c3b7` |
| FEX | `1cc4b93e7a71c883ec021b71359f136394dc1f3c` |
| OpenXR SDK | `8899a91c17ce9618f565f42408b47db1d6e9ccc7` |

Valve tag:
`experimental-wine-bleeding-edge-11.0-428201-20260904-pdf1645-w0509a0-dd7ac25-v35bdee`.

The workflow imports MS-ICU and Proton wineopenxr before applying the ordered
`wine-tkg-userpatches/*.mypatch` stack. The ARM64 Wine stack excludes the
wineopenxr import and `wineopenxr-host-runtime.mypatch`, keeping OpenXR in Proton.
No upstream workflow trigger or release behavior is changed by this port.

## Integration and behavior

The 29-file patch preserves Valve's netprofm `--prefer-native`, ICMP listener
and device-open behavior. The IPv6, loopback and tracing prerequisites are from
`ea887800fb7b`, `1d197dfb5568`, `9c0bf5fda079` and `91b081763ce7`.
Notification startup synchronization surrounds device publication and the
notification thread rather than replacing Valve NSI wholesale.

Linux topology uses NSI link/address/route notifications, operational carrier
and usable default routes, including on-link routes and excluding down/rejected
routes. Managers/networks/connections preserve identity and lifetime. Event sinks
use the COM GIT for apartment-safe dispatch and registered event typelib data.
The optional NetworkManager provider reads typed properties and authenticated
owner signals passively: no activation or connectivity probes. Missing D-Bus
falls back to topology. Monitor startup failure retains legacy snapshot behavior;
fatal runtime failures drain subscriptions and downgrade. NSI terminal failures
remain sticky until device-host restart; provider reconnect is separate.

The private strict IPv6 route query distinguishes unavailable from empty without
changing the public NSI/IP Helper success-empty contract. Fixed-width Unix calls
and explicit WoW64 thunks are retained. No multimedia, graphics or app-specific
production changes are introduced.

Fixtures use Valve winegcc's `-b TARGET`, link compiler runtimes after winecrt0,
honor `CC` and preserve the selected environment through sudo fallbacks. They
select built PE architectures, release disposable prefixes, and use the shared
wine64 tools for traditional split-i386 builds.

## Executed validation

Validation was performed in October 2026. Counts are recorded execution results,
not CI guarantees; workstation results were reported by the workstation operator.

| Configuration | Result |
| --- | --- |
| Direct-source native x64, D-Bus | Full build; 9 suite families, 6,265 assertions +254 D-Bus checks; zero failures, 53 todos, 3 skips. |
| Direct-source native x64, no D-Bus | 8 families, 6,019 assertions; zero failures, 45 todos, 3 skips; D-Bus suites not selected. |
| Direct-source new-WoW64, D-Bus | Full x64+i386 build; 12,530 assertions +508 D-Bus checks; zero failures, 106 todos, 6 skips. |
| Direct-source new-WoW64, no D-Bus | 12,038 assertions; zero failures, 90 todos, 6 skips; D-Bus suites not selected. |
| Final recipe native x64 | Full build; 9 families, 6,420 assertions +254 D-Bus checks; zero failures, 53 todos, one global-IPv6 skip. |
| Final recipe traditional split-i386 | Full build; same 6,420 assertions +254 D-Bus checks; zero failures, 53 todos, one IPv6 skip. |
| Installed runner STA fixtures | Topology200 and passive provider136 on each architecture: 672 assertions, zero failures. |
| Soda recipe smokes | Both architectures: Office WinRT classes, 1,024 LDAP checks each, ICU72.1/timezone and file-security reopen passed; Eagle build/package tests passed. |
| Native desktop Bottles | Both architectures: topology200 and provider136, total672 assertions, zero failures/skips; winecfg rendered and screenshot inspected. |
| Existing application prefix | Ordinary NLM tests on both architectures: 125 assertions each, zero failures/skips, 8 inherited todos each. |
| ARM64 | Clean patch replay/source assessment only; no compile/runtime pass. |

Direct-source builds disabled Wayland and FFmpeg due to host dependencies and
used a local OpenXR loader plus OpenCL link workaround. They are **not** the
release-compatible build. The subsequent actual pinned Wine-TKG recipe build
uses traditional split x64/i386 Unix libraries, release optimization flags,
Wayland, Vulkan, OpenGL, GStreamer and FFmpeg; only i386 wineopenxr is disabled.
It is a **non-PGO local candidate**, not a published PGO release. Inherited
optional OSSv4/Vosk/Piper warnings remain. No ARM SDK/compiler test was run.

The recipe's `_init`, `_prepare`, `_polish`, configure, build and package functions
were executed. Final preparation matched all 29 port files byte-for-byte.
Full clean 103-patch replay produced tree
`5b308a6b8c17b5d3f48b21f65694532d806480c5`; ARM's 102-patch replay produced
`be3effb6a8e3eb1c23ffbee4dadd744c86d286a1`. Generated source outputs are not
included in the patch. TKG's PCM/OpenCL preparation fixes are retained.

The build used a disposable Ubuntu24.04 overlay with RAM-backed writable state,
private mount/PID namespaces, masked homes/devices and denied service startup.
Dependencies were installed only inside the overlay. This is **not a pinned OCI
image**: retain package-version/apt-source manifests when reproducing. Noble's
conflicting multiarch crypto/libsecret/GStreamer/Samba development packages were
switched serially, retaining both runtime architectures. No host packages changed.

## Reproduction and tooling

Apply the pinned imports and ordered stack, run Valve generators
(`dlls/winevulkan/make_vulkan -v`, `tools/make_requests`, `tools/make_specfiles`,
`autoreconf -fiv`), then configure/build out of tree with tests enabled:

```sh
bash .github/check-netprofm.sh /absolute/WINE_SOURCE /absolute/WINE_BUILD /absolute/NEW_LOG_DIR
```

The wrapper uses sudo mount/network namespaces, `ip`, `setpriv`, `sysctl`, a C
compiler and Wine build tools. D-Bus suites need dbus-daemon, headers/pkg-config
and Python dbus/GLib. It covers ordinary NLM/NSI/IPHelper, topology/carrier/on-link,
strict IPv6 source failures, snapshot fallback, monitor faults, malformed/spoofed
D-Bus, real NSI EMFILE/ENOBUFS faults, cancellation and worker interleavings.
Reachability/reconnect tests isolate the provider bus connection; they do not
prove whole-Wine device-host/system-bus recovery or physical outages.

The release helpers are the executed local harness, not a one-command clean-room
installer. `netprofm-release-rootfs.sh` requires an Ubuntu24.04 host base and sudo;
it maps sibling Wine/source/build checkouts into generic `/inputs` and `/work`
paths. `netprofm-release-build.sh` expects the pinned TKG/build-tools config and
import checkpoint already staged in its scratch tree. Stage exact upstream
inputs and imports from the pinned Soda workflow before using it; local Git
checkpoint objects referenced by the harness are not published here. The normal
upstream workflow provides the recipe fetch/preparation sequence. Do not install
these dependencies on an unrelated host or treat the harness as a pinned image.

Packaging retains Mono10.4.1, OpenXR JSON, Eagle, licenses, patch and AI disclosure
in the standard Bottles runner layout. It does not invent runner metadata or
replace existing runners. No binary runner/runtime is distributed in this branch.
Logs, dependency manifests, source-cache objects and app data remain private.

`netprofm-bottles-launch.sh` invokes the real STA Windows observer through Bottles,
writing a quoted CRLF batch file after bootstrapping the prefix and requiring a
final zero-failure Windows summary. A spaced-path Win32 check passed 87 assertions.
The checked-in QA wrapper uses sudo/Xvfb and isolated XDG paths; its Flatpak
attempt did **not** pass. Successful native-desktop QA used a separate unprivileged
user/mount namespace wrapper with the host X11 socket. No claim that the Xvfb
wrapper itself passed native desktop QA is made.

## Application and runtime boundaries

The workstation reported a visible existing KakaoTalk application using the
candidate Wine/NLM/OpenGL and host Mesa, plus an inspected clean-account login
window. Existing application settings were retained apart from runner selection
and seven Wine-child runtime variables. This is not an authenticated-login,
message-delivery, physical suspend/outage or application media-playback assertion.
No account/chat capture is published. Synthetic codec tests are separate evidence.

See [runtime compatibility and per-child plugin paths](netprofm-runtime.md).
Arch requires the documented **host-first** library order; never apply private
library overrides globally to Bottles/Python or replace the host GPU/glibc stack.
Installed file checks passed, including the unchanged contents of Bottles'
two winemenubuilder files renamed to `.lock`.

Generic handle-close/live-driver-unload limits and comprehensive Linux
policy-routing/macOS/BSD coverage are outside this port. No ARM64 runtime,
hardware media/device functionality or upstream acceptance is claimed.
