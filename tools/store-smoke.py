#!/usr/bin/env python3
# ProsperoStore - One locked, bounded, exact-title console startup case.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import uuid

parser = argparse.ArgumentParser(__doc__)
parser.add_argument("--host", required=True)
parser.add_argument("--lock", type=Path, required=True)
parser.add_argument("--protocol", type=Path, required=True)
parser.add_argument("--ui-tools", type=Path, required=True)
parser.add_argument("--results", type=Path, required=True)
parser.add_argument("--close-prior", help="Exact previously identified title to close before the case")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("console_tour", args.ui_tools / "console-tour.py")
transport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transport)
title = json.loads((root / "sce_sys/param.json").read_text())["titleId"]
assert title == "PPSA99000"
if subprocess.check_output(["git", "status", "--porcelain"], cwd=root):
    raise SystemExit("Commit the exact candidate before hardware testing")
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
package = root / "dist" / title
files = sorted(path for path in package.rglob("*") if path.is_file())
if not files or not (package / "eboot.bin").is_file():
    raise SystemExit("Build the development candidate first")
args.results.mkdir(parents=True, exist_ok=False)
manifest = {str(path.relative_to(package)): hashlib.sha256(path.read_bytes()).hexdigest() for path in files}
(args.results / "candidate.json").write_text(json.dumps({
    "commit": commit, "files": manifest,
    "transport_sha256": hashlib.sha256((args.ui_tools / "console-tour.py").read_bytes()).hexdigest()
}, indent=2))
token = "prosperostore-" + uuid.uuid4().hex
while True:
    try:
        with args.lock.open("x") as lock:
            lock.write(token)
        break
    except FileExistsError:
        print("Console lock is held; waiting", flush=True)
        time.sleep(15)
done = threading.Event()
logger = None
console = None
result = {"classification": "no-run", "title": title, "commit": commit}
remote = "/data/homebrew/" + title
try:
    if not all(transport.port_open(args.host, port) for port in (2121, 3232, 9021)):
        raise RuntimeError("Required console services are unavailable")
    console = transport.Console(args.host, 2121, raw_self=True)
    names = console.names("/mnt/sandbox")
    if names is None:
        raise RuntimeError("Cannot establish an idle console")
    active = [name for name in names if name.startswith("PPSA")]
    if args.close_prior and active == [args.close_prior + "_000"]:
        prior = subprocess.run(
            ["bash", str(args.protocol / "scripts/send-controller.sh"), "close", args.close_prior, args.host, "9021"],
            env={**os.environ, "PS5_PAYLOAD_SDK": str(root / ".deps/native/ps5-payload-sdk")},
            capture_output=True, timeout=60)
        (args.results / "close-prior.log").write_bytes(prior.stdout + prior.stderr)
        if prior.returncode:
            raise RuntimeError("Exact prior title could not be closed")
        for _ in range(12):
            time.sleep(5)
            names = console.names("/mnt/sandbox")
            if names is None:
                raise RuntimeError("Console health became uncertain during preflight")
            active = [name for name in names if name.startswith("PPSA")]
            if not active:
                break
    if active:
        raise RuntimeError("Console has a running title: " + ", ".join(active))
    logger = threading.Thread(target=transport.record_klog,
                              args=(args.host, 3232, args.results / "klog.txt", done), daemon=True)
    logger.start()
    # Metadata and executable last; every uploaded file is read back by the shared helper.
    files.sort(key=lambda p: (p.name in ("eboot.bin", "param.json"), str(p)))
    for path in files:
        target = remote + "/" + path.relative_to(package).as_posix()
        data = path.read_bytes()
        if console.read(target) != data:
            console.write(target, data)
    deadline = time.monotonic() + 120
    while "mount.lnk" not in (console.names("/user/app/" + title) or []):
        if time.monotonic() >= deadline:
            raise RuntimeError("Title registration did not become ready")
        time.sleep(5)
    time.sleep(15)
    for relative, digest in manifest.items():
        data = console.read(remote + "/" + relative)
        if data is None or hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError("Remote verification failed: " + relative)
    console.close()
    console = None
    result["classification"] = "inconclusive"
    launched = subprocess.run(
        ["bash", str(args.protocol / "scripts/send-controller.sh"), "launch", title, args.host, "9021"],
        env={**os.environ, "PS5_PAYLOAD_SDK": str(root / ".deps/native/ps5-payload-sdk")},
        capture_output=True, timeout=60)
    (args.results / "launch.log").write_bytes(launched.stdout + launched.stderr)
    if launched.returncode:
        raise RuntimeError("Exact-title launch helper failed")
    print("Candidate verified and launched; observing 60 seconds", flush=True)
    log = b""
    for _ in range(12):
        time.sleep(5)
        console = transport.Console(args.host, 2121, raw_self=True)
        log = console.read("/data/prosperostore/logs/app.log") or console.read(
            f"/mnt/sandbox/{title}_000/download0/prosperostore/logs/app.log") or log
        (args.results / "app.log").write_bytes(log)
        crash = console.read("/data/prosperostore/logs/crash-latest.txt")
        if crash:
            (args.results / "crash.txt").write_bytes(crash)
        console.close()
        console = None
        if crash:
            result["classification"] = "failed"
            raise RuntimeError("App crash detected; do not retry")
    console = transport.Console(args.host, 2121, raw_self=True)
    console.write(remote + "/dev/request.txt", f"quit - {token}\n".encode())
    closed = False
    for _ in range(12):
        time.sleep(5)
        current = console.read("/data/prosperostore/logs/app.log")
        if current:
            log = current
            (args.results / "app.log").write_bytes(log)
        names = console.names("/mnt/sandbox")
        if names is not None and not any(name.startswith(title + "_") for name in names):
            closed = True
            break
    lifecycle = console.read("/data/shadowmount/debug.log") or b""
    (args.results / "shadowmount.log").write_text("\n".join(
        line for line in lifecycle.decode(errors="replace").splitlines() if title in line))
    if closed:
        console.ftp.sendcmd("DELE " + remote + "/dev/request.txt")
    healthy = all(transport.port_open(args.host, port) for port in (2121, 3232, 9021))
    result.update(closed=closed, healthy=healthy)
    required = (b"interactive width=3840 height=2160", b"first-swap ok", b"teardown complete")
    done.set()
    logger.join(timeout=5)
    receipts = log + (args.results / "klog.txt").read_bytes()
    passed = closed and healthy and all(marker in receipts for marker in required)
    result["classification"] = "pass" if passed else "failed"
    if not passed:
        raise RuntimeError("Startup or teardown criterion failed; inspect saved evidence")
finally:
    if console:
        console.close()
    done.set()
    if logger:
        logger.join(timeout=5)
    (args.results / "result.json").write_text(json.dumps(result, indent=2))
    if args.lock.read_text() == token:
        args.lock.unlink()
print(json.dumps(result), flush=True)
