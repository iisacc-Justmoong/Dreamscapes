#!/usr/bin/env python3
"""Check the packaged Helper/account dependency without launching or signing in."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


def verify(apk, ndk):
    readers = list((ndk / "toolchains/llvm/prebuilt").glob("*/bin/llvm-readelf"))
    if len(readers) != 1:
        raise RuntimeError("The NDK must provide one host llvm-readelf executable")
    names = ("libiiSocietyHelper.so", "libiiAcountManager.so")
    with zipfile.ZipFile(apk) as archive, tempfile.TemporaryDirectory(
        prefix="native-bundle-check-", dir=apk.parent
    ) as temporary:
        for name in names:
            member = f"lib/arm64-v8a/{name}"
            if member not in archive.namelist():
                raise RuntimeError(f"APK is missing the runtime dependency: {member}")
            binary = Path(temporary) / name
            binary.write_bytes(archive.read(member))
            header = subprocess.check_output([str(readers[0]), "-h", str(binary)], text=True)
            if not re.search(r"Machine:\s+AArch64\b", header):
                raise RuntimeError(f"APK library is not Android arm64: {name}")
        dynamic = subprocess.check_output(
            [str(readers[0]), "-d", str(Path(temporary) / names[0])], text=True
        )
        if "Shared library: [libiiAcountManager.so]" not in dynamic:
            raise RuntimeError("Packaged Helper does not link the packaged account manager")
    print("Android arm64 Helper and account runtime dependencies verified; no account requests made.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--ndk", type=Path, required=True)
    args = parser.parse_args()
    verify(args.apk.resolve(strict=True), args.ndk.resolve(strict=True))
