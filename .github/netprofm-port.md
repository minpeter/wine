# Dynamic NLM connectivity: proposed Soda integration

This local submission branch starts at official `bottlesdevs/wine:soda`
revision `b9e41a2512f69c895d6a44c6a9d64fb1cfbaeb97` (Soda 11.0-28
Experimental). Its existing patches, build configuration and workflows are
unchanged. It adds the NLM/required NSI patch and a focused test wrapper.

AI-assisted implementation and port using Amp, authored by Woonggi Min.
Existing copyright/LGPL notices and prerequisite attribution to Hans Leidekker
and Paul Gofman are retained. The patch extends the reviewed
`minpeter/wine:port/soda-11.0-10-nlm` payload with strict IPv6 stream-error
handling, bounded Linux reachability setup, complete netlink datagram reception
and listener-startup regression coverage. Its SHA256 is
`61df4c95520ef8f989f5fcef1e5224370f3fdb8ca6d52c870f9961f85f161d04`.
It has not been accepted upstream.

## Behavior

- Linux NLM connectivity follows NSI link, address and route notifications.
  Adapter carrier and usable default routes determine topology connectivity.
  The Linux monitor sizes each netlink datagram before receiving it, growing
  its buffer when necessary instead of losing notifications above 4,096 bytes.
  The driver initializes the kernel listener before publishing its device, so
  subscription readiness also guarantees that initial changes are captured.
  Initialization failure rejects monitoring without removing enumeration/ICMP.
- An optional passive NetworkManager provider can downgrade Internet status.
  It does not activate NetworkManager or initiate connectivity probes.
  Unavailable D-Bus falls back to topology, not automatic disconnection.
  Linux loads `libsystemd.so.0` at runtime for nonblocking sd-bus transport;
  no systemd SDK or required link dependency is added. The existing D-Bus
  configure switch still disables the provider. Missing runtime symbols,
  unsupported non-Unix address overrides and timeout all retain topology.
  Each provider invocation has a one-second monotonic budget covering
  authentication, Hello, match registration and queries; teardown never flushes.
- COM event sinks use apartment-safe GIT dispatch. Startup reconciles state
  after all subscriptions are armed; backend failure completes pending/future
  requests and downgrades monitoring rather than stranding subscriptions.
- Public IPv6 NSI enumeration preserves its partial/empty results on source
  failure. A private strict selector rejects open and stream-read failures for
  NLM, keeping unavailable enumeration distinct from a valid empty snapshot.
- Existing public NSI layouts, netprofm exports, Valve prefer-native behavior
  and independent ICMP/device handling are preserved.

## Validation and submission status

The preceding payload was compiled for x64 and traditional i386 against the
official Soda 11.0-10 base, then tested in its complete official runtime with
only the paired NLM/NSI modules replaced. Ordinary NLM/NSI/IP Helper, dynamic
topology, topology policy, IPv6 availability, malformed D-Bus, backend failure
and provider-reconnect fixtures passed. Separate no-D-Bus checks passed.
Native Bottles clean/upgraded-prefix NLM, typelib registration and isolated
network fixtures also passed on both architectures. Expected TODOs and
environmental IPv6 skips remain.

The stream-error fix was checked with freshly rebuilt NSI Unix modules and NLM
test PEs on both architectures: all four source modes passed, including public
partial-row preservation. The original x64 backend failed the new read-error
and partial-read-error cases. Ordinary NLM/NSI/IP Helper and all six network
fixture families passed with zero failures/skips. The exact updated wrapper
also passed on the complete retained x64 reference build (legacy CROSS flags);
i386 used the genuine paired-module configuration and complete private official
11.0-10 runtime, not a full-tree rebuild or an i386 wrapper execution.

For delivery, both NSI Unix modules and updated NLM test PEs were subsequently
rebuilt in the corrected full-ancestry paired configurations, without the legacy
CROSS fallback. A separate candidate kept the previously verified official PGO
11.0-28 + NLM runtime and replaced only these two Unix modules: all other 4,553
regular files and 16 symlinks were unchanged. The module/source/paired-PE hashes,
ELF architectures, imports and runtime dependency closure were verified.

On this complete 11.0-28 candidate, both x64 and classic i386 passed ordinary
NLM (87), NSI (1,807), IP Helper (1,111), all four IPv6 source modes
(110/111/110/110), and native Bottles dynamic topology (200) and passive
NetworkManager/reconnect (136), with zero failures/skips and inherited TODOs
retained. Networking and prefixes were isolated. Original 11.0-28 NLM modules
reproduced two real read-error assertion failures on both architectures.
A controlled workstation restart verified the application's new runner/NLM
mappings, the updated NSI driver module, and a visible application window;
graphics, sync, environment and DLL overrides were preserved.

The subsequent bounded-bus change was compiled in both corrected paired-module
configurations, including the compile-time no-provider branches. A separate
native execution of the actual provider exercised silent AUTH, a saturated
accept queue, and stalled Hello/AddMatch/GetNameOwner/GetAll: initial/reconnect
invocations returned within approximately one second and teardown returned
immediately. Adapted malformed-message checks use the real sd-bus encoder and
reader: 1,387 checks passed on each architecture, along with ordinary tests and
all six existing fixture families in the complete private 11.0-10 runtime.
The expanded public COM fixture passed six stall phases (52 assertions each)
and OFFLINE-to-ONLINE plus stalled-reconnect release (20 assertions), with every
wire target confirmed on both architectures. No failures or skips were hidden.

The complete 11.0-28 workstation candidate replaced only the two `netprofm.so`
files. All other 4,553 regular files and 16 symlinks matched the prior runner.
On both architectures it passed the same six public COM stall phases and
recovery/release control, ordinary NLM/NSI/IP Helper (87/1,807/1,111), all four
IPv6 source modes, and native Bottles topology/NM (200/136), with zero failures
or skips and inherited TODOs retained. Maximum observed COM creation was
1,033 ms (x64) / 1,031 ms (i386), release 758 / 744 ms, and GetConnectivity 0 ms.
This workstation run used the exact compiled standalone PE and Python harness;
the exact build-and-run shell fixture passed separately on the retained stable
runtime. The full wrapper was not run in partial module trees; syntax, path
guards and shared-tool resolution passed.

After these checks, the workstation selected the separately installed candidate.
The application's actual loader, NLM PE/Unix library and NSI driver mappings
were verified, along with visible compositor windows. Only its runner selection
changed; the prior runner and a stopped-prefix backup were retained. No live
system-bus failure or authenticated application function was exercised.

The subsequent netlink fix was built without warnings in both corrected paired
configurations. Genuine kernel multipath-route notifications at 253 nexthops
(4,088 bytes) and 254 nexthops (4,104 bytes) passed public NSI addition/deletion
completion and NLM LOCAL-to-INTERNET-to-LOCAL checks: 25 assertions each, zero
failures/skips, on both architectures. The genuine prior backend passed the
smaller boundary but failed five behavioral assertions at the larger boundary
on both architectures. Ordinary tests and all seven fixture scripts passed in
the complete private 11.0-10 runtime. Initial supplemental x64 cold-COM and
provider-restart failures remain retained; clean final and genuine prior-backend
comparisons passed under matching isolated setups, but their initial cause is
unproven. The added allocator header does not explain those failures.

The complete 11.0-28 candidate replaced only the two `nsiproxy.so` files;
all other 4,553 regular files and 16 symlinks matched the preceding runner.
Both architectures passed both public netlink boundary cases, ordinary
NLM/NSI/IP Helper (87/1,807/1,111), native Bottles topology/NM (200/136), and
direct-Wine IPv6 source modes (110/111/110/110), with zero failures/skips and
inherited TODOs retained. An initial Bottles-adapter IPv6 invocation did not
preserve the requested fault mode and was not accepted as validation; the
unchanged tests were rerun directly through Wine. The prior current28 x64
backend independently reproduced the five larger-boundary assertion failures.
After isolated QA, a controlled application restart verified the selected
candidate's actual loader, NLM and updated NSI mappings and a mapped compositor
window. Only runner selection changed; the preceding runner and stopped-prefix
backup remain available. No account content or workstation paths are published.

The subsequent listener-startup review reproduced a smaller-route race on the
actual 11.0-28 x64 runtime: subscriptions and snapshot reconciliation could
complete before the detached worker bound the multicast socket. Adding a route
during that gap left cached LOCAL connectivity stale until another event. The
fix binds synchronously before publishing the device and latches initialization
failure. The internal dispatch table gains an eighth initialization entry;
the original seven positions and all parameter layouts are unchanged. Both PE
and Unix halves must be replaced together, not as a Unix-only overlay.

Both architectures were rebuilt without warnings in the corrected paired
configurations. The separate complete 11.0-28 candidate replaces exactly four
production files: both `nsiproxy.sys` and `nsiproxy.so` pairs. All other 4,551
regular files and 16 symlinks match the preceding runner. Delivered standalone
fixtures pass on both architectures: device interleavings (1,005), socket/bind
failures (34 each), receive/retained errors (148 each), control (149), and both
kernel multipath boundaries (25 each). During each five-second startup bind
gate, the launcher remains alive and the factory does not finish early. Both
unchanged LOCAL topology and a default added during the gate initialize
correctly after binding (five assertions each), without a later wake event.
Ordinary tests (87/1,807/1,111), native Bottles topology/NM (200/136), and the
unchanged direct-Wine IPv6 source script (110/111/110/110) pass on both
architectures, with zero failures/skips and inherited TODOs retained.

The independent complete stable 11.0-10 reference also passes ordinary tests
and all seven fixture scripts on both architectures. Genuine rebuilt prior
PE/Unix pairs publish the manager early in both gated startup modes; adding
the default during the gate also leaves stale LOCAL connectivity. The unchanged
full x64 fault-script trace rerun holds the gate for over five seconds and then
observes listener readiness before creation, passing both startup modes.

Initial supplemental stable x64 runs failed before assertions at the startup
launcher and the IPv6 empty-mode launcher. Their logs remain unsuppressed.
Focused unchanged-source Unix-path and Windows-path startup contrasts pass;
the path hypothesis is not confirmed, and the initial causes remain unproven.
A separate traced IPv6 diagnostic used a mismatched older test PE and was
rejected as production evidence; the unchanged script with the matching PE
passes all four modes. No failing assertions were suppressed.
No fixture, assertion or production implementation was changed for those
diagnostic comparisons. Current28 checks above are composed-runtime execution,
not a full compile-and-run wrapper execution in the partial module build trees.

After current28 QA, the workstation selected the separately installed candidate.
A controlled application restart verified its actual loader, NLM PE/Unix and
paired NSI driver PE/Unix mappings, and a mapped, non-hidden application window.
Only the runner setting changed; the preceding runner and protected stopped
prefix backup remain available. No authenticated function or whole-system
reboot/suspend acceptance is claimed.

These are composed-runtime checks, **not a fresh full current patch-stack or
dual-architecture PGO release-build pass**. Full current-stack compilation and
release validation remain pending before submission. A visible application
window does not establish authenticated login, message delivery or suspend
recovery. No binary runner or private workstation data is included here.

## Local iteration workflow

For runtime-affecting local changes: review, fix, rebuild/test a separate
candidate, replace the local runner and verify execution, then push. Retain
the previous runner and a stopped-prefix backup for rollback; never replace
modules in a running runner.

After building the prepared source and tests, run the focused wrapper once per
build tree (it requires passwordless sudo and private mount/network namespaces):

```sh
.github/check-netprofm.sh /path/to/prepared-wine /path/to/build /path/to/new-logs
```

The wrapper, source, build and new log directory must resolve outside `/tmp`:
the wrapper replaces `/tmp` with an isolated fixture workspace. Symlinks into
`/tmp` are rejected before logs are created or privileged commands run. Use a
checkout/build under your home directory and place logs there as well.
For split-i386 builds, relative `wine64dir` values are resolved against the
32-bit build directory, independently of the wrapper's working directory.
Run `.github/tests/check-netprofm-paths.sh` for the unprivileged path-validation
and shared-tool resolution checks; their minimal build stand-ins do not run Wine
or real network fixtures. The real IPv6 availability fixture covers open failure,
successful emptiness, immediate read failure and failure after a valid route row.

The inherited Soda workflow discovers the new `.mypatch` automatically. No CI
or release workflow was triggered or changed to prepare this local branch.

## Known limits

Terminal NSI backend failure remains sticky until the driver-host restart;
the affected manager falls back to a snapshot, not recovered monitoring.
Provider reconnect tests do not prove whole-Wine device-host/system-bus
recovery. Inherited policy-routing limitations remain. Native macOS/BSD,
physical outage and real suspend/hibernate restoration are unverified.
The provider budget bounds protocol waits, not loader/filesystem stalls,
OS scheduling or arbitrary application COM callbacks. Linux needs an available
sd-bus runtime for the optional provider; non-Linux retains topology fallback.
GUI wheel/focus fixtures and empty-login rendering do not validate authenticated
KakaoTalk chat scrolling, message delivery or multimedia.
