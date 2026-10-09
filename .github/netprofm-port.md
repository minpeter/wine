# Dynamic NLM connectivity: proposed Soda integration

This local submission branch starts at official `bottlesdevs/wine:soda`
revision `b9e41a2512f69c895d6a44c6a9d64fb1cfbaeb97` (Soda 11.0-28
Experimental). Its existing patches, build configuration and workflows are
unchanged. It adds the NLM/required NSI patch and a focused test wrapper.

AI-assisted implementation and port using Amp, authored by Woonggi Min.
Existing copyright/LGPL notices and prerequisite attribution to Hans Leidekker
and Paul Gofman are retained. The patch extends the reviewed
`minpeter/wine:port/soda-11.0-10-nlm` payload with strict IPv6 stream-error
handling and regression coverage. Its SHA256 is
`c4ead8734afaf27279a6fe8214ae3907f9e85f1ac0ec9a3fb7f3d0e1db09bb29`.
It has not been accepted upstream.

## Behavior

- Linux NLM connectivity follows NSI link, address and route notifications.
  Adapter carrier and usable default routes determine topology connectivity.
- An optional passive NetworkManager provider can downgrade Internet status.
  It does not activate NetworkManager or initiate connectivity probes.
  Unavailable D-Bus falls back to topology, not automatic disconnection.
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
GUI wheel/focus fixtures and empty-login rendering do not validate authenticated
KakaoTalk chat scrolling, message delivery or multimedia.
