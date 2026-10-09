# Dynamic NLM connectivity: proposed Soda integration

This local submission branch starts at official `bottlesdevs/wine:soda`
revision `b9e41a2512f69c895d6a44c6a9d64fb1cfbaeb97` (Soda 11.0-28
Experimental). Its existing patches, build configuration and workflows are
unchanged. It adds the NLM/required NSI patch and a focused test wrapper.

AI-assisted implementation and port using Amp, authored by Woonggi Min.
Existing copyright/LGPL notices and prerequisite attribution to Hans Leidekker
and Paul Gofman are retained. The patch is identical to the reviewed
`minpeter/wine:port/soda-11.0-10-nlm` payload, SHA256
`946abf98ab06a2541321d72e456f1b057d50d5d69a8ec8290a3491bced41d6d4`.
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
- Public IPv6 NSI enumeration preserves successful empty results on unsupported
  backends. A private strict selector is used only for NLM enumeration.
- Existing public NSI layouts, netprofm exports, Valve prefer-native behavior
  and independent ICMP/device handling are preserved.

## Validation and submission status

The identical patch was compiled for x64 and traditional i386 against the
official Soda 11.0-10 base, then tested in its complete official runtime with
only the paired NLM/NSI modules replaced. Ordinary NLM/NSI/IP Helper, dynamic
topology, topology policy, IPv6 availability, malformed D-Bus, backend failure
and provider-reconnect fixtures passed. Separate no-D-Bus checks passed.
Native Bottles clean/upgraded-prefix NLM, typelib registration and isolated
network fixtures also passed on both architectures. Expected TODOs and
environmental IPv6 skips remain.

Those results are prior-base evidence, **not a build/runtime pass for this
latest-Soda branch**. Full current patch-stack preparation, compilation and
runtime QA remain pending before submission. There is no full clean
dual-architecture release-build claim. No binary runner is included here.

After building the prepared source and tests, run the focused wrapper once per
build tree (it requires passwordless sudo and private mount/network namespaces):

```sh
.github/check-netprofm.sh /path/to/prepared-wine /path/to/build /path/to/new-logs
```

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
