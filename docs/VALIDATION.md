# Current console case

M0 startup smoke: title `PPSA99000`, firmware 6.02, owner-authorized console,
FTP 2121, klog 3232, elfldr 9021. The changed variable is the store application
on the pinned native/UI foundations. There is no qualified store baseline yet;
this case does not claim installation, security-gate, or performance completion.

Acceptance: every file in `dist/PPSA99000` reads back identically, exact-title
registration is present, the app reaches a first swap at 3840×2160, logs frame
timing during 60 seconds, honors its development quit request, completes
teardown, disappears from the sandbox list, and leaves all services healthy.
Stop on a crash report, service loss, unexpected active title, or ambiguous
registration. No automatic retry after execution.

`tools/store-smoke.py` requires a clean committed checkout and uses the UI
foundation's verified FTP helper and the protocol's exact-title launch helper.
The environment lock is held only for this bounded case. The result directory
contains source commit, candidate hashes, launch receipt, logs and classification.
Raw evidence stays ignored under `results/`.

Host checks: `make test lint`, `make DEVELOPMENT=1 app`, and
`make host-snapshots`. The catalog live probe is `bash tools/store-probe.sh`.
