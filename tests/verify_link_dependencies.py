"""Check actual generated target properties and the linked leaf executable."""
from pathlib import Path
import subprocess
import sys


def verify(manifest: Path, lifetime_executable: Path, packaged_runner: Path) -> None:
    targets = dict(line.split("=", 1) for line in manifest.read_text().splitlines())
    assert not targets["directory"], "Directory-wide libraries leak into unrelated executables"
    generation = set(targets["generation"].split("|"))
    assert "iiSharedCanvas::Quick" in generation, generation
    assert "iiSharedCanvas::iiSharedCanvas" not in generation, generation
    assert {"iiFileProvider::iiFileProvider", "iiFileProvider::Native"} <= generation, generation
    assert "iiSDKCore::Runtime" in generation, generation
    assert "iiFileProvider" not in targets["lifetime"], targets["lifetime"]
    assert "iiSDKCore" not in targets["lifetime"], targets["lifetime"]
    assert lifetime_executable.is_file(), lifetime_executable
    assert packaged_runner.is_file(), packaged_runner
    forbidden = ("DreamscapesGeneration", "LVRS", "iiLocalDiffusion", "iiMultimodalAgent",
                 "Qt6::Quick", "Qt6::Qml", "Qt6::Widgets")
    assert not any(name in targets["packaged"] for name in forbidden), targets["packaged"]
    if sys.platform == "darwin":
        libraries = subprocess.check_output(["/usr/bin/otool", "-L", str(lifetime_executable)], text=True)
        assert "libiiFileProvider" not in libraries, libraries
        assert "libiiSDKCore" not in libraries, libraries
        libraries = subprocess.check_output(["/usr/bin/otool", "-L", str(packaged_runner)], text=True)
        assert not any(name in libraries for name in ("QtQuick", "QtQml", "QtWidgets", "libLVRS",
            "libiiLocalDiffusion", "libiiMultimodalAgent")), libraries
    print("Product-owned dependencies and the independent lifetime executable passed")


if __name__ == "__main__":
    verify(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]))
