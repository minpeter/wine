# Private Ubuntu runtime compatibility packaging

AI-assisted with Amp. These tools build a separate relocatable compatibility
bundle, not an Arch installation or a replacement of the Soda runner. No bundle
binaries, application data or private diagnostics are distributed in this branch.

The disposable Ubuntu24.04 rootfs supplies both architectures of FFmpeg6
(`libavcodec.so.60`, `libavformat.so.60`, `libavutil.so.58`), pcap0.8, GStreamer,
xkbcommon/registry, USB, OpenCL ICD loader, gphoto, sane, capi and pcsclite, plus
x64 OpenXR loader. Install base/good/bad/ugly/libav plugins only inside the
isolated dependency environment. `netprofm-runtime-bundle.py` copies SONAMEs,
recursive dependencies, scanners and gphoto dlopened transports without binary
editing; it requires the retained overlay environment and a new output directory.
No SANE backend, vendor ICD, OpenXR runtime, driver or system configuration is copied.

The executed bundle contained 1,247 ELF files from 569 binary packages, including
266 x64 and 255 i386 plugin files. Generated `PACKAGES.tsv`/`ORIGINS.tsv` identify
package/source versions and source paths; `licenses/` retains per-package notices
and common licenses. Apt sources are retained. GPL/LGPL corresponding-source and
other redistribution obligations must be satisfied before distributing binaries;
the local bundle does not contain corresponding source tarballs.

**Excluded:** all libc6 files, glibc/ld-linux/libc/libm/libdl/libpthread/librt,
GL/EGL/GLES/GLX/GLdispatch/OpenGL, GBM, DRM, Vulkan loader and Mesa/DRI.
`HOST-LIBRARIES.tsv` lists dependencies supplied by the host. Keep a complete
matching host GPU stack; never use fake ABI SONAME symlinks.

## Per-Wine-child environment

Extract a privately generated bundle to its own directory, not `/usr/lib` or
the runner. Choose the Windows application's architecture, not the launcher.
**On Arch use host-first library ordering.** Private-first Ubuntu readline is
incompatible with Arch's bash-based child shell. Both architecture probes and
the child shell passed with the order below. The archived helper defaults to
private-first; explicitly override its child environment:

```sh
B=/absolute/soda-nlm-ubuntu24-runtime-20261009
R=/absolute/runner
SODA_RUNTIME_CACHE=/absolute/disposable/cache \
  "$B/run-with-runtime.sh" i386 env \
  LD_LIBRARY_PATH="/usr/lib:/usr/lib32:$B/lib/x86_64-linux-gnu:$B/lib/i386-linux-gnu" \
  "$R/bin/wine" application.exe
```

For x64 select `x86_64`. The following i386 values belong only to the Wine child:

| Variable | Value (substitute absolute paths) |
| --- | --- |
| `LD_LIBRARY_PATH` | `/usr/lib:/usr/lib32:/bundle/lib/x86_64-linux-gnu:/bundle/lib/i386-linux-gnu` |
| `GST_PLUGIN_SYSTEM_PATH_1_0` | `/bundle/lib/i386-linux-gnu/gstreamer-1.0` |
| `GST_PLUGIN_PATH_1_0` | Same private plugin directory; no host plugin mixture |
| `GST_PLUGIN_SCANNER_1_0` | `/bundle/libexec/i386-linux-gnu/gst-plugin-scanner` |
| `GST_REGISTRY_1_0` | `/disposable/cache/i386-linux-gnu/registry.bin` |
| `CAMLIBS`, `IOLIBS` | Versioned private gphoto camera/transport directories |

For x64 use x86_64 plugin/scanner/cache paths with the same host-first library
order. Keep separate architecture registries. In Bottles set these in the
disposable bottle's Wine-child `Environment_Variables`, or use a per-executable
wrapper. **Never export them into the Bottles GUI, Python/GTK, shell startup,
Flatpak globals or system environment.** Host graphics ABIs remain untouched.

## Reported coverage and limits

The rootfs audit checked architecture, relative symlinks and 6,382 DT_NEEDED
edges: all non-host dependencies bundled, no glibc/GPU replacements or local
build-path RUNPATH. Four vendor RUNPATHs are recorded separately. Both architecture
probes passed requested SONAME loading, 14 factory checks, synthetic audio and
H.264 encode/decode pipelines reaching EOS. Source is `netprofm-runtime-probe.c`.

The workstation operator reported all 1,857 runtime file hashes passing, candidate
Unix modules resolving, host Mesa GLX/gallium/Vulkan dependencies resolving, and
both architecture probes passing with host-first ordering. Child-shell checks also
passed. Native Bottles topology200/provider136 on each architecture passed with
zero failures/skips; ordinary existing-prefix NLM tests passed 125 on each with
zero failures/skips and 8 inherited todos. Winecfg and a clean-account application
login UI were rendered and screenshots inspected; the existing application was
reported visibly running on candidate Wine/NLM/OpenGL and host Mesa.

These are not authenticated-login/message-delivery assertions, physical
suspend/outage tests, hardware telephony/device tests or application media-playback
results. Synthetic media pipelines do not establish application playback. No
account/chat captures or workstation operational diagnostics are published.
The checked-in sudo/Xvfb orchestration and failed Flatpak attempt are distinct
from successful workstation unprivileged/native-desktop QA. See
[port validation and reproduction limits](netprofm-port.md).
