#!/usr/bin/env python3
import subprocess
import sys
import re
import plistlib
from pathlib import Path

binary = sys.argv[1]
if sys.platform == "darwin":
    qt_library = re.compile(r"^(?:lib)?Qt(?:(?:[0-9]+)?[A-Za-z_]|[0-9]*(?:\.|$))")
    executable = Path(binary).resolve()
    checked = set()
    crypto_versions = set()

    def inspect(path, inherited_rpaths):
        path = path.resolve()
        if path in checked:
            return
        checked.add(path)
        load_commands = subprocess.run(["otool", "-l", str(path)], check=True, text=True, capture_output=True).stdout
        rpaths = []
        for raw in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset \d+\)", load_commands):
            rpaths.append(raw.replace("@loader_path", str(path.parent)).replace("@executable_path", str(executable.parent)))
        rpaths += inherited_rpaths
        output = subprocess.run(["otool", "-L", str(path)], check=True, text=True, capture_output=True).stdout
        for line in output.splitlines()[1:]:
            if not line[:1].isspace():
                continue  # A universal library has an additional architecture heading.
            dependency = line.strip().split(" (compatibility version", 1)[0]
            crypto = re.search(r"libcrypto\.(\d+)\.dylib$", dependency)
            if crypto:
                crypto_versions.add(crypto.group(1))
            if qt_library.match(dependency.rsplit("/", 1)[-1]) or "LVRS" in dependency:
                raise SystemExit(f"native application links a Qt or LVRS library: {dependency}")
            if dependency.startswith(("/usr/lib/", "/System/Library/")):
                continue
            if dependency.startswith("@rpath/"):
                candidates = [Path(folder) / dependency.removeprefix("@rpath/") for folder in rpaths]
                found = next((candidate for candidate in candidates if candidate.is_file()), None)
                if found is None:
                    raise SystemExit(f"native application dependency is not resolvable: {dependency}")
            else:
                value = dependency.replace("@loader_path", str(path.parent)).replace("@executable_path", str(executable.parent))
                found = Path(value)
                if not found.is_file():
                    raise SystemExit(f"native application dependency is not resolvable: {dependency}")
            inspect(found, rpaths)

    inspect(executable, [])
    if len(crypto_versions) > 1:
        raise SystemExit(f"native application loads incompatible OpenSSL major versions: {sorted(crypto_versions)}")
    bundle = executable.parents[2]
    info = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())
    if not info.get("CFBundleShortVersionString") or not info.get("CFBundleVersion"):
        raise SystemExit("native app bundle is missing its version metadata")
    for name in (info.get("CFBundleIconFile"), "Pretendard-Regular.ttf"):
        if not name or not (bundle / "Contents/Resources" / name).is_file():
            raise SystemExit(f"native app bundle is missing a visual resource: {name}")
    if not info.get("NSCameraUsageDescription") or not info.get("NSPhotoLibraryUsageDescription"):
        raise SystemExit("native app bundle is missing its camera or photo permission description")
    remaining = [path for path in bundle.rglob("*") if path.is_file() and path.suffix.lower() == ".qml"]
    if remaining:
        raise SystemExit(f"native app bundle contains a QML file: {remaining[0].relative_to(bundle)}")
    signed = subprocess.run(["codesign", "--verify", "--strict", str(bundle)], text=True, capture_output=True)
    if signed.returncode:
        raise SystemExit(f"native app bundle signature is invalid: {signed.stderr.strip()}")
