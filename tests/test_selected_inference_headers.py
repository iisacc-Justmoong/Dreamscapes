#!/usr/bin/env python3
"""Check that the selected inference SDK wins over an older CPATH header."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

database = Path(sys.argv[1])
resources = Path(sys.argv[2])
entries = json.loads(database.read_text(encoding="utf-8"))
entry = next(item for item in entries if item["file"].endswith("/DreamscapesMcp.cpp"))
arguments = entry.get("arguments") or shlex.split(entry["command"])
command = []
skip = False
for argument in arguments:
    if skip:
        skip = False
        continue
    if argument in ("-o", "-MF", "-MT", "-MQ"):
        skip = True
        continue
    if argument in ("-c", "-MD", "-MMD"):
        continue
    command.append(argument)
command.append("-fsyntax-only")
with tempfile.TemporaryDirectory(prefix="header-precedence-", dir=resources) as temporary:
    conflict = Path(temporary) / "Generation" / "NativeDiffusion.hpp"
    conflict.parent.mkdir()
    conflict.write_text('#error "An unrelated CPATH inference header was selected"\n', encoding="utf-8")
    environment = os.environ.copy()
    environment["CPATH"] = temporary + os.pathsep + environment.get("CPATH", "")
    result = subprocess.run(command, cwd=entry["directory"], env=environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise SystemExit("The product did not compile against its selected inference SDK:\n" + result.stderr)
