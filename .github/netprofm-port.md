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

These results are prior-base evidence, **not a build/runtime pass for this
latest-Soda branch**. Full current patch-stack preparation, compilation and
runtime QA remain pending before submission. There is no full clean
dual-architecture release-build claim. No binary runner is included here.

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
