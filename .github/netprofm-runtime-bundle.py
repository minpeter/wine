#!/usr/bin/python3
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
"""Package a private runtime from the disposable, retained Ubuntu rootfs."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True)


assert os.environ.get("HOME") == "/work/home"
assert output("findmnt", "-n", "-o", "FSTYPE", "/").strip() == "overlay"
destination = Path("/work/runtime-bundle/soda-nlm-ubuntu24-runtime-20261009")
assert not destination.exists(), "Keep previous bundles; use a new output path."
destination.mkdir(parents=True)
architectures = ("x86_64-linux-gnu", "i386-linux-gnu")
seeds = (
    "libavcodec.so.60", "libavformat.so.60", "libavutil.so.58",
    "libswresample.so.4", "libpcap.so.0.8", "libgstreamer-1.0.so.0",
    "libgstbase-1.0.so.0", "libgstaudio-1.0.so.0", "libgstvideo-1.0.so.0",
    "libgstgl-1.0.so.0", "libxkbcommon.so.0", "libxkbregistry.so.0",
    "libusb-1.0.so.0", "libOpenCL.so.1", "libgphoto2.so.6",
    "libgphoto2_port.so.12", "libsane.so.1", "libcapi20.so.3", "libpcsclite.so.1",
)
# These must remain supplied by the workstation, not replaced by Ubuntu's
# glibc or GPU stack. All other transitive DT_NEEDED libraries are bundled.
host_pattern = re.compile(
    r"^(?:ld-linux.*|lib(?:c|m|dl|pthread|rt|resolv|util|anl|nss_.*)\.so.*|"
    r"lib(?:GL|EGL|GLES.*|GLX.*|GLdispatch|OpenGL|glapi|gbm|drm.*|vulkan)\.so.*)$"
)
metadata = {}
for line in output(
    "dpkg-query", "-W", "-f=${binary:Package}\t${Version}\t${Architecture}\t"
    "${source:Package}\t${source:Version}\t${db:Status-Status}\n"
).splitlines():
    fields = line.split("\t")
    if fields[-1] == "installed":
        metadata[fields[0]] = fields[1:5]
owners = {}
for listing in Path("/var/lib/dpkg/info").glob("*.list"):
    package = listing.name[:-5]
    if package in metadata:
        for filename in listing.read_text().splitlines():
            owners[filename] = package


def owner(path):
    name = str(path)
    return owners.get(name) or owners.get(name.replace("/usr/lib/", "/lib/", 1))


included = []
excluded = []
packages = set()
seen = set()
for architecture in architectures:
    source_dir = Path("/usr/lib") / architecture
    lib_dir = destination / "lib" / architecture
    lib_dir.mkdir(parents=True)
    loader = "ld-linux-x86-64.so.2" if architecture == "x86_64-linux-gnu" else "ld-linux.so.2"
    loader_path = (source_dir / loader).resolve()
    excluded.append((architecture, loader, str(loader_path), owner(loader_path)))
    roots = [source_dir / name for name in seeds]
    if architecture == "x86_64-linux-gnu":
        roots.append(source_dir / "libopenxr_loader.so.1")
    plugins = sorted((source_dir / "gstreamer-1.0").glob("*.so"))
    scanner = source_dir / "gstreamer1.0/gstreamer-1.0/gst-plugin-scanner"
    assert scanner.is_file()
    roots.append(scanner)
    roots.extend(plugins)
    # Camera transports are dlopened, not recorded in DT_NEEDED.
    for directory in ("libgphoto2", "libgphoto2_port"):
        roots.extend(sorted((source_dir / directory).glob("*/*.so")))
    queue = [(path, True) for path in roots]
    while queue:
        path, root = queue.pop(0)
        assert path.is_file(), path
        real = path.resolve()
        package = owner(real)
        assert package, f"Unowned library is not reproducible: {real}"
        basename = path.name
        if host_pattern.fullmatch(basename) or package.split(":")[0] == "libc6":
            excluded.append((architecture, basename, str(real), package))
            continue
        assert "/dri/" not in str(real)
        assert not package.split(":")[0].startswith(("mesa-", "libgl1-mesa", "libegl-mesa", "libglx-mesa"))
        if root and path in plugins:
            target_dir = lib_dir / "gstreamer-1.0"
        elif root and path == scanner:
            target_dir = destination / "libexec" / architecture
        elif root and any(part in ("libgphoto2", "libgphoto2_port") for part in path.parts):
            target_dir = lib_dir / path.relative_to(source_dir).parent
        else:
            target_dir = lib_dir
        target_dir.mkdir(parents=True, exist_ok=True)
        target = target_dir / real.name
        if basename != real.name:
            alias = target_dir / basename
            if not alias.exists():
                alias.symlink_to(real.name)
        key = (architecture, str(real), str(target_dir))
        if key in seen:
            continue
        seen.add(key)
        shutil.copy2(real, target)
        packages.add(package)
        included.append((str(target.relative_to(destination)), str(real), package))
        linkage = output("ldd", str(real))
        assert "not found" not in linkage, (real, linkage)
        for line in linkage.splitlines():
            match = re.match(r"\s*(\S+) => (/\S+) \(", line)
            if match:
                soname, resolved = match.groups()
                dependency = Path(resolved)
                if host_pattern.fullmatch(soname):
                    excluded.append((architecture, soname, str(dependency.resolve()), owner(dependency.resolve()) or "host"))
                else:
                    queue.append((dependency, False))
    (destination / "SEEDS.txt").open("a").write(
        architecture + "\n" + "\n".join(str(path.relative_to(source_dir)) for path in roots) + "\n"
    )

licenses = destination / "licenses"
for package in sorted(packages):
    copyright_file = Path("/usr/share/doc") / package.split(":")[0] / "copyright"
    assert copyright_file.is_file(), (package, copyright_file)
    directory = licenses / package
    directory.mkdir(parents=True)
    shutil.copy2(copyright_file, directory / "copyright")
shutil.copytree("/usr/share/common-licenses", licenses / "common-licenses")
shutil.copy2("/etc/os-release", destination / "BUILD-OS-RELEASE")
shutil.copy2("/etc/apt/sources.list.d/ubuntu.sources", destination / "ubuntu.sources")
(destination / "PACKAGES.tsv").write_text(
    "binary-package\tversion\tarchitecture\tsource-package\tsource-version\n" +
    "".join("\t".join([package] + metadata[package]) + "\n" for package in sorted(packages))
)
(destination / "ORIGINS.tsv").write_text(
    "bundle-file\trootfs-file\tbinary-package\n" +
    "".join("\t".join(row) + "\n" for row in sorted(included))
)
(destination / "HOST-LIBRARIES.tsv").write_text(
    "architecture\tsoname\trootfs-reference-only\tpackage-not-bundled\n" +
    "".join("\t".join(row) + "\n" for row in sorted(set(excluded)))
)
(destination / "COUNTS.json").write_text(json.dumps({
    "copied_elf_files": len(included), "binary_packages": len(packages),
    "plugins": {arch: len(list((destination / "lib" / arch / "gstreamer-1.0").glob("*.so")))
                for arch in architectures},
}, indent=2) + "\n")
print(destination)
print((destination / "COUNTS.json").read_text())
