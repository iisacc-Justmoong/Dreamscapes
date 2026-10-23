#!/usr/bin/env python3
"""Check that focused CTest groups retain every Qt GUI test function."""

import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-executable", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--ctest", default="ctest")
    args = parser.parse_args()
    functions = subprocess.check_output(
        [str(args.test_executable), "-functions"], text=True
    )
    available = set(re.findall(r"^(\w+)\(\)$", functions, re.MULTILINE))
    if not available:
        raise RuntimeError("Qt returned no GUI test functions")
    registry = json.loads(subprocess.check_output(
        [args.ctest, "--test-dir", str(args.build_dir), "--show-only=json-v1"],
        text=True,
    ))
    covered, unknown, unscoped = set(), set(), []
    groups = 0
    for test in registry["tests"]:
        command = test["command"]
        if Path(command[0]).resolve() != args.test_executable.resolve():
            continue
        groups += 1
        if len(command) == 1:
            unscoped.append(test["name"])
        for selection in command[1:]:
            if selection.startswith("-"):
                continue
            if selection not in available:
                unknown.add(selection)
            else:
                covered.add(selection)
    missing = available - covered
    passed = not (missing or unknown or unscoped)
    print(json.dumps(dict(
        passed=passed, guiFunctions=len(available), groups=groups,
        coveredFunctions=len(covered), missing=sorted(missing),
        unknown=sorted(unknown), unscopedGroups=unscoped,
    ), indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
