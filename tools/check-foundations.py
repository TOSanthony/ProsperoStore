# ProsperoStore - Reject drift in imported foundation files.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
expected = {}
for foundation in json.loads((root / "FOUNDATIONS.json").read_text()):
    expected.update(foundation["files"])
for name, digest in expected.items():
    if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
        raise SystemExit("Foundation changed in place: " + name)
for dependency in json.loads((root / "third_party/STORE_SOURCES.json").read_text()):
    for name, digest in dependency["files"].items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
            raise SystemExit("Vendored source changed in place: " + name)
print(f"Verified {len(expected)} unchanged foundation files")
