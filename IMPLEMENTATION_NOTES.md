# ProsperoStore implementation notes

Status: 2026-10-02. Implementation baseline: `6934406`. Title ID: `PPSA99000`.

This is a development snapshot, not a finished store or release. Browsing,
verified catalog access, artwork, app details, search, and installed-library
discovery are implemented. **Install, update, and uninstall are not enabled.**
`PLAN.md` remains the specification; the distinctions below separate implemented
code from features qualified on the console.

## Implemented

| Area | Current behavior |
| --- | --- |
| Native application | PS5 build and packaging, 4K rendering, controller navigation, audio, diagnostic logging, and development-only remote exit requests. |
| Storefront | Discover, Apps, Games, Tools, Coming soon, Installed, and Updates sections. The layout follows the UI library's Storefront design (featured banner, section chips, card grid, product page with a frosted action box) in the Farlight colours of its Aurora Shelf design (owner's choice; Glass Orchard until 2026-10-03). While the catalog or the library loads, an arc draws itself into a full circle. Artwork regions are square to preserve 512x512 app icons; coming-soon apps without artwork show `assets/images/coming-soon.png`. |
| Install engine | `src/install/`: verified download (size and SHA-256 from the signed catalog, three attempts), ZIP validation from the archive's directory, exact space checks, unpacking into staging, single-rename activation, update by backup swap, uninstall, receipts, a journal and start-up recovery. Host-tested only; it has never run on a console. |
| Installer worker | Development builds only (`STORE_INSTALLER`, set by `DEVELOPMENT=1`), and only when elevated. One transaction at a time on its own worker, a queue of up to 16, journal recovery before the first job, an inventory rescan after each, and a result notice. The product page's button installs into `/data/homebrew`, shows the phase and a progress bar, and cancels. Updates and uninstalls are refused until the running-app check exists; uninstall asks first. Release builds leave the button at rest and say why. |
| Update notice | Once per launch, after the catalog refresh, the boilerplate's update-check decision code runs over the store's own HTTPS for `PPSA99000`. A newer listed version shows a top-right notice for ten seconds. Unknown shows nothing. |
| Catalog trust | Ed25519 verification of the catalog manifest, SHA-256 verification of catalog documents, bounded JSON parsing, schema checks, sequence rollback protection, and verified offline cache. |
| HTTPS | Elevated PacBrew curl/OpenSSL with certificate verification using the console CA list, URL/redirect restrictions, bounded responses, cancellation, and connection reuse. |
| Artwork | Background loading, persistent cache, bounded PNG decoding, a bounded result queue, and limited texture uploads per frame. The artificial delay between icon requests was removed. |
| Search and sorting | Triangle opens the native keyboard; name/developer filtering and R3 sorting by name, newest release, or recently updated. Native keyboard interaction still needs console qualification. |
| App details | Scrollable verified metadata and release notes, full-size artwork, retry behavior, and a QR code linking to the app's canonical homebrew.page page. |
| Installed inventory | Read-only bounded scans using ShadowMountPlus locations/depth, folder metadata parsing, image-file listing, duplicate-title detection, and receipt-based ownership. |
| Updates view | Newer catalog versions are shown for managed installations. This is discovery/display only; it does not perform updates. |
| State storage | Persistent store state uses `/data/prosperostore` after elevation: cache, logs, receipts, and development controls. Browsing falls back to memory when elevation is unavailable. Pre-elevation sandbox logging is disabled. |
| Filesystem probes | Development checks inspect configured locations and test create/rename/cleanup behavior. These are not yet the production location-selection workflow. |

## Implementation structure

- `src/app/`: screen state and background service orchestration.
- `src/catalog/`: signed catalog parsing/client, cache, versions, artwork, and
  installed metadata/receipt parsing.
- `src/net/`: HTTP policy and the shared curl request implementation.
- `src/install/`: archive validation and unpacking, link-refusing file helpers,
  and the journaled install/update/uninstall transactions with recovery.
- `src/system/`: scan-location policy, filesystem probes, and installed inventory.
- `src/main.cpp`: native application lifecycle and delivery of worker results to
  the UI; `host/store_main.cpp` supplies host previews and screen checks.
- `tests/store_*` and `tools/store-test.sh`: store-specific host checks.
- `tools/store-smoke.py`: locked, bounded console validation with exact title,
  artifact verification, run tokens, exit checks, and service-health checks.

The background service currently uses separate catalog/detail and icon workers.
The render thread consumes a bounded result queue. Curl handles retain reusable
connections while resetting request-specific options. Workers stop before curl
cleanup. Icon requests use the cache before the network; first-run downloads can
still take longer, but current hardware evidence does not establish final
cold-cache versus warm-cache loading times.

Inventory results are advisory snapshots. Future transactions must revalidate
paths, ownership, running status, and space immediately before changing files.
Receipts must match title ID, location, and installed version to establish
ownership. Duplicate IDs disable management. Image files are listed without
mounting them and currently have no resolved title identity. Local-only artwork,
full image discovery parity, and stale-receipt cleanup remain incomplete.

## Foundations and dependencies

`FOUNDATIONS.json` records the complete adopted file sets and hashes:

- Native boilerplate: `209914dc8a5ee67ff0bd044dfd40d18f877eae2c`.
- Homebrew UI: `3cb6db940035614f86cabd87ebf6e21b14ff694e`.
- Rendering uses the pinned ps5-opengl SDK 1.0.0 tooling configuration.

Foundation files are imported as whole pinned sets. Generic fixes belong in the
foundation first; store-specific screens, catalog, system policy, and installer
logic belong here. The adopted source is included in this repository. Foundation
development commits may still be local to their separate repositories; uploading
this snapshot does not imply those repositories were published or updated.

Vendor versions, licenses, and source hashes are recorded in
`third_party/STORE_SOURCES.json`, `third_party/STORE_NOTICES.md`, the other
third-party notices, and `assets/licenses/`. No proprietary system libraries or
local SDK cache are included in this source snapshot.

## Validation completed

- Signed-catalog, cache, HTTP/TLS policy, icon, filesystem, and worker checks have
  passed during development, including AddressSanitizer/UndefinedBehaviorSanitizer
  runs. TLS tests cover connection reuse and rejection of invalid trust/hosts.
- The inventory change (`5e7c287`) passed its sanitized parser/inventory tests,
  integrated service checks, targeted clang-tidy checks, and native build.
  Checks cover receipt mismatches, duplicates, scan depth, cancellation,
  symlink rejection, and malformed/oversized metadata.
- Host previews were inspected for square artwork, search/empty states, details,
  installed titles, and updates. A rendered QR code was independently decoded.
- On PS5 firmware 6.02, earlier committed candidates demonstrated elevated
  catalog access, 4K rendering, artwork loading, and token-matched clean exit.
- Internal storage and M.2 filesystem create/rename/cleanup probes passed.
  This does not qualify every drive, failure mode, or read-only case.
- The latest frozen console candidate (`59a8868`) loaded the verified catalog
  and 12 icons, exited on its own development request, and left services healthy.
  After an initial loading spike, observed frame batches averaged 16.68 ms.

These are cumulative development results, **not a claim that the latest complete
source has passed every check on hardware**. The installed-inventory UI, native
keyboard, and newest curl/artwork changes still need their full console scenarios.

Raw build logs, screenshots, console captures, and generated packages remain
local in ignored `build/`, `results/`, and `dist/` directories. `PLAN.md` contains
the compact milestone index; those local evidence paths are not GitHub downloads.

## Known limitations and unresolved investigations

1. Automated launch remains unresolved. The protocol controller now initializes
   its context size and reports the native result (separate protocol commit
   `09eebf4`). The latest case reported `0x80940010` before a later app launch;
   it proves the observed lifecycle, not that the controller fix succeeded.
   Manual app launches work according to the owner. The current smoke runner
   requires an explicit successful native launch acknowledgement.
2. Steady 60 fps across all required scenarios is not qualified. Earlier builds
   had periodic stalls near one second. Synchronous driver profiling output is
   a hypothesis; a profiling-disabled SDK experiment exists locally, but a
   matched controlled comparison has not been completed. The latest frozen
   package differed from the earlier control and cannot establish causality.
3. Installed scans run after catalog refresh attempts, so network failures can
   delay initial library discovery. Local-only entries currently use placeholders.
4. The install engine exists and passes its host tests (hostile archives, every
   refusal, a simulated power cut at every step of install, update and
   uninstall, and fuzzing of the ZIP and JSON readers), but it has never run on
   a console. Development builds connect it to the product page; release builds
   do not. Still missing: the running-title check (the owner supplies the call;
   until then every update and uninstall is refused), a queue screen, location
   selection (new installs go to `/data/homebrew`), a confirmation when the
   store is closed during a job (closing cancels it cleanly), a scripted
   install request for console runs, and self-update.
5. Settings/location selection, first-run notices, localization, full accessibility
   and polish review, interruption tests, and release soak testing remain open.

## Remaining work under PLAN.md

| Milestone | Remaining qualification or implementation |
| --- | --- |
| M1 | Production location selection and read-only/unavailable-drive gates. |
| M2 | Real artifact verification, bounded download streaming, retry/resume qualification. |
| M3 | Native keyboard, latest artwork/cache behavior, offline browsing, and performance qualification. |
| M4 | Engine done on the host (validation/limits, space checks, extraction, atomic activation, refusal tests, fuzzing). The installer worker and the page's install, progress and cancel are in development builds. Remaining: a queue screen and a real install on a console. |
| M5 | Complete inventory/image handling, safe receipt cleanup, running-title guard, update/uninstall, and recall actions. |
| M6 | Self-update gate and workflow. |
| M7 | Journal recovery, interruption simulation at each mutation, removed-drive and network-failure handling. |
| M8 | Settings, languages, notices, and final interface polish. |
| M9 | Adoption of the shared update-check kit by another Prospero app. |
| M10 | Full review, soak tests, release packaging/tagging, and catalog publication. |

The user requires store-owned files under `/data/prosperostore`. PLAN.md also
describes same-filesystem staging/backup on other installation drives. Resolve
that location constraint before implementing external-drive transactions;
cross-filesystem renames cannot provide the required atomic activation.

## Build and verification commands

Use the configured WSL environment and repository root:

```sh
make DEVELOPMENT=1 app
make test lint
make host-snapshots
python3 tools/check-foundations.py
python3 tools/store-smoke.py --self-test
```

Console tests additionally require the configured protocol and UI transport
checkouts, an idle console, an owned environment lock, an exact committed
candidate, and bounded teardown/health verification. Do not interpret a successful
TCP payload transfer as a successful launch. No release or deployment is implied
by uploading this source snapshot.
