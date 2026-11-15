#!/usr/bin/env python3
"""Create minimal LVUI presentations and an inventory of legacy view inputs.

The generated presentations are static. They are not a QML interpreter or a
behavioral translation. Run once before retiring the original source files.
"""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
VIEWS = ROOT / "src/App"
MANIFEST = ROOT / "resources/migration/view-manifest.json"
STRING = re.compile(r'\b(?:text|title|label|placeholderText|heading)\s*:\s*(?:qsTr\(\s*)?"((?:\\.|[^"\\])*)"')


def title(stem):
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", stem)


def static_text(source):
    found = []
    for match in STRING.finditer(source):
        try:
            text = json.loads('"' + match.group(1) + '"')
        except ValueError:
            continue
        text = " ".join(text.split())
        if not text or "%1" in text or "%2" in text or len(text) > 180:
            continue
        if text not in found:
            found.append(text)
    return found


def convert(path, index):
    source = path.read_text(encoding="utf-8")
    labels = static_text(source)
    original = path.relative_to(ROOT).as_posix()
    replacement = path.with_suffix(".lvui")
    if replacement.exists():
        raise RuntimeError(f"refusing to overwrite existing LVUI view {replacement}")
    lines = [
        "import vm NativeMigratedViewBackend",
        'EventListener(id: "migratedSurface", width: 0, height: 0, flex: 1, onSizeChanged: vm.sizeChanged) {',
        '    ScrollView(id: "migratedScroll", width: vm.viewportWidth, height: vm.viewportHeight) {',
        '        VStack(id: "migratedContent", width: vm.viewportWidth, spacing: Metric.space8,',
        '               leftPadding: Metric.space16, rightPadding: Metric.space16,',
        '               topPadding: Metric.space16, bottomPadding: Metric.space16, alignment: .stretch) {',
        f'            Label(id: "migratedHeading", text: {json.dumps(title(path.stem), ensure_ascii=False)}, style: .header2)',
    ]
    for number, label in enumerate(labels):
        lines.append(f'            Label(id: "migratedText{number}", text: {json.dumps(label, ensure_ascii=False)}, style: .body)')
    lines.extend(["        }", "    }", "}", ""])
    replacement.write_text("\n".join(lines), encoding="utf-8")
    return {
        "source": original,
        "replacement": replacement.relative_to(ROOT).as_posix(),
        "sha256": hashlib.sha256(source.encode("utf-8")).hexdigest(),
        "name": f"MigratedView{index:03d}",
        "textCount": len(labels),
    }


def main():
    paths = sorted(VIEWS.rglob("*.qml"))
    if not paths or MANIFEST.exists():
        raise RuntimeError("source inventory missing or already converted")
    views = [convert(path, index) for index, path in enumerate(paths)]
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps({"schema": 1, "views": views}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Created {len(views)} static LVUI views; QML sources are retained until validation.")


if __name__ == "__main__":
    main()
