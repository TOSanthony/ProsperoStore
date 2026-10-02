# ProsperoStore dependencies

The app is GPL-3.0-or-later. Its pinned foundation code and assets retain their
original notices in [UI_NOTICES.md](UI_NOTICES.md) and the root
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

Additional sources are pinned by commit and SHA-256 in
[STORE_SOURCES.json](STORE_SOURCES.json). Their full licenses are alongside the
vendored files:

| Library | Purpose | License |
| --- | --- | --- |
| Monocypher 4.0.2 | Ed25519 verification | BSD-2-Clause or CC0-1.0 |
| yyjson 0.10.0 | Bounded JSON parsing | MIT |
| miniz 3.0.2 | ZIP inspection and extraction | MIT |
| PicoSHA2 | SHA-256 | MIT |

Host previews use the host's Mesa/EGL and libcurl. Console HTTPS uses the
platform's networking libraries. Proprietary system modules are not included.
