#!/usr/bin/env python3
import subprocess
import sys
import re
from pathlib import Path

binary = sys.argv[1]
if sys.platform == "darwin":
    forbidden = ("QtQml", "QtQuick", "Qt6Qml", "Qt6Quick", "LVRS")
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
            if any(name in dependency for name in forbidden):
                raise SystemExit(f"native application links a QML, Qt Quick or LVRS view library: {dependency}")
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
    signed = subprocess.run(["codesign", "--verify", "--strict", str(executable.parents[2])], text=True, capture_output=True)
    if signed.returncode:
        raise SystemExit(f"native app bundle signature is invalid: {signed.stderr.strip()}")
