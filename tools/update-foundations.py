# ProsperoStore - Update each foundation as a complete pinned set.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
manifest = root / "FOUNDATIONS.json"
records = json.loads(manifest.read_text())
if len(sys.argv) != len(records) + 1:
    raise SystemExit("usage: update-foundations.py <boilerplate checkout> <UI checkout>")
for record, argument in zip(records, sys.argv[1:]):
    source = Path(argument).resolve()
    if subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True):
        raise SystemExit("Commit foundation changes before importing")
    remote = subprocess.check_output(["git", "-C", str(source), "remote", "get-url", "origin"], text=True).strip()
    if remote.removesuffix(".git").replace("git@github.com:", "https://github.com/") != record["repository"]:
        raise SystemExit("Foundation repository does not match")
for record, argument in zip(records, sys.argv[1:]):
    source = Path(argument).resolve()
    # Include newly added files in the already adopted example families.
    tracked = subprocess.check_output(["git", "-C", str(source), "ls-files"], text=True).splitlines()
    if record["repository"].endswith("/ps5-native-app-boilerplate"):
        for name in tracked:
            if name.startswith("examples/") or name in ("tests/test_crash_report.cpp", "tests/test_https_trust.cpp"):
                record["files"].setdefault(name, "")
    for name in record["files"]:
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        if not (root / name).is_file() or (source / name).read_bytes() != (root / name).read_bytes():
            shutil.copyfile(source / name, root / name)
        record["files"][name] = hashlib.sha256((root / name).read_bytes()).hexdigest()
    record["commit"] = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
manifest.write_text(json.dumps(records, indent=2) + "\n")
