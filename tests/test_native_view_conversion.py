#!/usr/bin/env python3
"""Check that the product view inventory and LVUI replacements agree."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
manifest_path = root / "resources/migration/view-manifest.json"

def fail(message):
    raise AssertionError(message)

def main():
    if not manifest_path.is_file():
        fail("missing QML-to-LVUI migration inventory")
    manifest = json.loads(manifest_path.read_text())
    views = manifest.get("views", [])
    if not views:
        fail("empty view inventory")
    sources = set()
    for view in views:
        source, replacement = view["source"], view["replacement"]
        if source in sources or not source.endswith(".qml") or not replacement.endswith(".lvui"):
            fail("invalid or duplicate migration entry")
        sources.add(source)
        if len(view.get("sha256", "")) != hashlib.sha256().digest_size * 2:
            fail(f"missing legacy source hash: {source}")
        if (root / source).exists():
            fail(f"legacy QML view remains: {source}")
        migrated = root / replacement
        backend = view.get("backend", "NativeMigratedViewBackend")
        if not migrated.is_file() or f"import vm {backend}" not in migrated.read_text():
            fail(f"missing LVUI view or view model: {replacement}")
    remaining = list((root / "src/App").rglob("*.qml"))
    if remaining:
        fail(f"QML views remain under src/App: {remaining[0]}")
    main_source = (root / "src/App/Native/NativeMain.cpp").read_text()
    if "NativeApplicationWindow" not in main_source or "QQml" in main_source:
        fail("application does not start from the native window")
    spec = importlib.util.spec_from_file_location("dreamscapes_views", root / "src/App/Native/dreamscapes_views.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    catalog = module.ViewCatalog(manifest_path)
    if len(catalog.views()) != len(views) or not catalog.find("src/App/Views/Editor/CanvasEditor.lvui"):
        fail("Python view inventory does not match the compiled view inventory")
    print(f"{len(views)} LVUI replacements; no QML views under src/App")

if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, KeyError, ValueError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
