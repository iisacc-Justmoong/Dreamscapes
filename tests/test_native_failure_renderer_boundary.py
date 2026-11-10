#!/usr/bin/env python3
"""Check the native failure view without inspecting or launching the product app."""
import pathlib
import re
import subprocess
import sys

generated = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
assert "<LVUI/NativeModal.h>" in generated and "<LVUI/NativeCodeEditor.h>" in generated
assert "<LVUI/NativeTextEditor.h>" in generated
assert "<LVRS/" not in generated and "lvrs::" not in generated
root = pathlib.Path(__file__).resolve().parents[1] / "src/App/Views/Failure"
for name in ("NativeFailureDetails.h", "NativeFailureDetails.cpp", "NativeFailureDetailsView.h",
             "NativeFailureDetailsView.cpp", "NativeFailureDetails.lvui"):
    source = (root / name).read_text(encoding="utf-8")
    assert not re.search(r'^\s*#\s*include\s*[<"](?:Qt|Q[A-Z]|LVRS/)', source, re.M), name
    assert not re.search(r'\b(?:QObject|QQuickItem|QVariant|QQml\w*|lvrs::)\b', source), name
if sys.platform == "darwin":
    executable = sys.argv[2]
    deps = subprocess.run(["otool", "-L", executable], check=True, capture_output=True, text=True).stdout
    assert not re.search(r"(?i)(libLVRS|Qt(?:Core|Gui|Qml|Quick))", deps), deps
    symbols = subprocess.run(["nm", "-g", executable], check=True, capture_output=True, text=True).stdout
    assert not re.search(r"\b_+ZN4lvrs", symbols), "The native view contains LVRS symbols"
