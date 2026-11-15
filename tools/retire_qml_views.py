#!/usr/bin/env python3
"""Retire only the QML files whose verified LVUI replacements were built."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / "resources/migration/view-manifest.json").read_text())
paths = []
for item in manifest["views"]:
    source = root / item["source"]
    replacement = root / item["replacement"]
    if source.suffix != ".qml" or replacement.suffix != ".lvui" or not replacement.is_file():
        raise RuntimeError(f"invalid replacement for {source}")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if digest != item["sha256"]:
        raise RuntimeError(f"source changed after conversion; preserving every QML file: {source}")
    paths.append(source)
for source in paths:
    source.unlink()
print(f"Retired {len(paths)} verified QML view files.")
