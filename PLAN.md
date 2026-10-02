# ProsperoStore: implementation plan

Draft 1, 2026-10-02. Title ID **PPSA99000** (reserved in the catalog).

ProsperoStore is a native PS5 app store for the homebrew catalog at
[homebrew.page](https://homebrew.page). With the controller, from the couch,
you browse the catalog, install an app, see what you have installed, and keep
it up to date. It manages three things and nothing else: **install, uninstall
and update.**

**The bar:** it should feel like a first-party store. Beautiful, polished,
fast: 4K at a steady 60 frames per second, no screen that waits on the network,
no install that can leave the console in a broken state.

It is built on `ps5-native-app-boilerplate` and `ps5-opengl` (OpenGL 4.6 Core,
SDK 1.0.0), with the interface taken from `ps5-homebrew-ui` (Homebrew UI Lab:
its component library and its `store` design). The catalog side is the store
API specified in the catalog repository's `docs/api.md`. Console work follows
`ps5-agent-runbook`.

Every statement about the console below is marked with how well it is known:

| Mark | Meaning |
| --- | --- |
| **[proven]** | Seen working on a console in one of our apps |
| **[source]** | Read from source code or documentation, not tried by us |
| **[assumed]** | A working assumption the owner accepted; to be confirmed when it is first used |
| **[open]** | Not known; a milestone finds out |

---

## 1. Scope

### In the first release

| Feature | What the user gets |
| --- | --- |
| **Browse** | The whole catalog as a grid of tiles: all, apps, games, tools, coming soon. Search, filter and sort. |
| **App page** | Name, developer, description, version, size, release date, license, source, and a button for the one thing that can be done next: Install, Update, Uninstall, or nothing. |
| **Install** | Download, verify, unpack and put in place, with progress and a clear result. ZIP artifacts only. |
| **Installed** | Everything found in the install locations, with its installed version. Apps the store didn't install are listed and marked as not managed. |
| **Updates** | Apps the store manages whose catalog version is higher than the installed one, with Update and Update all. Includes the store itself. |
| **Uninstall** | Removes an app the store installed. |
| **Install location** | A setting: any location ShadowMountPlus scans (section 3.3). |
| **Running-app guard** | An app that is running can't be updated or uninstalled. |
| **Works offline** | The last catalog and all icons it has seen stay available; installing needs the network. |

### Not in the first release

| Left out | Why |
| --- | --- |
| Image artifacts (`.ffpkg`, `.ffpfsc`) | Owner's decision: ZIP first. 14 of the 15 listed apps ship a ZIP. Images are shown with "can't be installed by this version". |
| Launching apps | Owner's decision: the store manages install, uninstall and updates. Apps are started from the home screen. |
| Taking over apps installed by hand | Owner's decision: they are listed with a note, nothing more. |
| Restarting itself after its own update | Owner's decision: the store asks the user to restart it. |
| Waiting for ShadowMountPlus | Owner's decision: after an install the store says that ShadowMountPlus will add the app to the home screen shortly. |
| Other catalogs, accounts, ratings, payments | Out of scope. |

---

## 2. What it builds on

| Piece | Used for | State |
| --- | --- | --- |
| Store API, `https://homebrew.page/api/v1/` | Catalog data, versions, icons | Live. Specified in the catalog's `docs/api.md`; versioned (`schema` 2). |
| `ps5-native-app-boilerplate` | Build, packaging, runtime, title layout | In use by every Prospero app. |
| Sandbox elevation (`docs/SANDBOX_ELEVATION.md` in the boilerplate) | Reaching `/data` and the other install locations | Used by ProsperoEden. |
| `ps5-opengl` SDK 1.0.0 | Rendering | Passed the OpenGL 4.6 conformance run. |
| `ps5-homebrew-ui` | Components, themes, sound, the `store` design, the PC host renderer | All 21 designs validated on a console at 4K, 16.68 ms average frame. |
| `sceHttp` / `sceSsl` / `sceNet` | HTTPS | **[proven]** in ProsperoTV, ProsperoRadio and ProsperoLichess, with certificate checks. |
| ShadowMountPlus | Puts installed apps on the home screen | Its behaviour below is **[source]** (branch 1.7). |

---

## 3. Facts that shape the plan

### 3.1 The sandbox and elevation

- A native title's sandbox has no `/data`. **[proven]**
- The boilerplate's elevation request gives the process filesystem access
  outside the sandbox. It needs a compatible `elfldr` listening on loopback
  port 9021, a helper ELF built for this title ID and shipped in the app, and
  it must run during single-threaded startup, before any worker starts.
  **[source]**; `/data` access through it is **[proven]** in ProsperoEden.
- Access to `/mnt/ext0`, `/mnt/ext1` and `/mnt/usb*` through the same
  elevation is **[open]** (milestone M1).
- Elevation can fail (no loader, unsupported firmware). The store must then
  still open, in a **read-only mode**: browse and see details, with a clear
  explanation of why nothing can be installed.

### 3.2 Networking

- HTTPS with certificate verification works through `sceHttp` with the system
  certificate store. **[proven]** against lichess.org.
- The same against `homebrew.page` (Cloudflare) and GitHub's release file host
  (a redirect from `github.com` to `release-assets.githubusercontent.com`) is
  **[assumed]**. Fallback agreed with the owner: curl. Note for that path:
  libcurl with PacBrew OpenSSL failed to start in a native app before.
- `sceHttpReadData` returns only when the buffer it was given is full.
  **[proven]** Downloads use a large buffer; progress is reported per buffer.
- `sceHttpAbortRequest` from another thread unblocks a read. **[proven]** This
  is how Cancel works.
- A timeout shows as `0x80431068`. **[proven]**
- Whether `sceHttp` decompresses gzip, and whether `Range` requests work for
  resuming, are **[open]**. Neither is required: the API files are small
  uncompressed, and a failed download can restart.
- Certificate verification is never switched off to make something work.

### 3.3 ShadowMountPlus

- It scans `/data/homebrew`, `/data/etaHEN/games`, `/mnt/ext0` and `/mnt/ext1`
  (their `homebrew` and `etaHEN/games` folders), and `/mnt/usb0` to `/mnt/usb7`
  (the same). A full scan runs every 15 seconds, and a folder is picked up
  about 10 seconds after it stops changing. **[source]**
- **One copy per title ID.** A second folder or image of the same title gives
  a "duplicate titleId" notice. **[source]** The store never leaves two.
- It copies a title's metadata (`sce_sys`) into the system **once**; an
  updated app keeps the icon and version the console first recorded.
  **[source]** Consequence: the store reads the installed version from the
  app's own `sce_sys/param.json` on disk, never from the console's title
  database.
- Staging and backup folders must be outside every scanned location, or
  ShadowMountPlus would register half-written apps.

### 3.4 The catalog

- Each app has two versions. `version` is the developer's release tag, for
  display. `content_version` is the `contentVersion` of the release's
  `param.json` (`NN.NNN.NNN`) and is the only value compared.
- **An update exists when the catalog's `content_version` is higher than the
  installed `contentVersion`.** Unknown on either side means "unknown", never
  "update".
- Not every listed app has a usable `content_version` yet: some repositories
  don't hold a `param.json`, and several developers never raise it. Those apps
  install fine and simply never show an update.
- `sha256` and `artifact_url` are never missing for an available app.
- `icon_hash` tells the store when an icon changed, so icons are downloaded
  once.

---

## 4. Decisions

Made by the owner on 2026-10-02 unless marked as a default.

| # | Decision |
| --- | --- |
| D1 | Name ProsperoStore, title ID PPSA99000, private repository until release. |
| D2 | ZIP artifacts only in the first release. |
| D3 | The install location is a setting, limited to locations ShadowMountPlus scans. |
| D4 | The installed version is the `contentVersion` in the app's `param.json`. |
| D5 | The store detects updates for other apps and for itself. |
| D6 | Filesystem access uses the boilerplate's elevation mechanism. |
| D7 | Networking is assumed to work with `sceHttp`; curl is the fallback. |
| D8 | After an install, the store tells the user ShadowMountPlus will add the app; it doesn't wait for or verify the registration. |
| D9 | A running app is never updated or uninstalled. The owner supplies the system call that reports whether a title is running. |
| D10 | After the store updates itself, it asks the user to restart it. If replacing itself can't be made safe, telling the user to update it by hand is acceptable. |
| D11 | The store does not launch apps. |
| D12 | Apps installed outside the store are listed with a note that the store doesn't manage them. No actions on them. |
| D13 | Uninstall is part of the first release. |
| D14 | The interface is based on Homebrew UI Lab. |
| D15 *(default)* | Uninstall removes the app's folder only. The app's own saved data (`/user/download/<TITLEID>` and anything the app wrote elsewhere) is left alone and the dialog says so. |
| D16 *(default)* | The store's own files live in `/data/prosperostore` (settings, receipts, cached catalog and icons, journal), and in a `prosperostore` folder at the top of each other drive it installs to (staging only). |
| D17 *(default)* | Pre-releases are offered like any other release, because the catalog lists one current release per app. |

---

## 5. Features in detail

### 5.1 Browse and search

- Data: `index.json`, fetched at start and on demand, with `If-None-Match`.
  The last good copy is kept on disk and shown immediately at the next start;
  the fresh one replaces it when it arrives. The header shows when the catalog
  was last refreshed and whether the store is offline.
- Icons: `icon_small` for tiles, `icon` for the app page, kept on disk with
  their `icon_hash` and fetched again only when the hash changes. A few
  downloads run at a time, nearest tiles first; a tile shows a placeholder
  until its icon arrives and never blocks scrolling.
- Sections: All, Apps, Games, Tools, Coming soon (reservations), plus
  Installed and Updates.
- Search by name and developer with the system keyboard; sort by name, newest
  release, recently updated.
- Each tile carries a state badge: Installed, Update, Coming soon, or nothing.

### 5.2 App page

- Data: `apps/<TITLEID>.json`, fetched when the page opens (cached with its
  `ETag`).
- Shows: icon, name, developer, kind, description, release tag, release date,
  download size, license, source repository, and installed version when there
  is one.
- One primary action, chosen by state (section 5.9).
- A QR code and the short address of the app's page on homebrew.page, for the
  release notes and the source on a phone.
- A standing line: the app comes from its developer, who is responsible for
  it; some apps need a payload or extra setup described in their release
  notes.

### 5.3 Install

One install runs at a time; others wait in a queue the user can see and edit.

1. **Refuse early.** Not a ZIP; already installed outside the store; the
   target location is missing or read-only; not enough free space (the
   download plus an estimate of the unpacked size, plus a margin); no
   elevation.
2. **Download** `artifact_url` to the staging folder, following redirects,
   computing SHA-256 as bytes arrive. Progress shows bytes, speed and time
   left. Cancel aborts the request and deletes the partial file.
3. **Verify.** The hash must equal the catalog's `sha256`. A mismatch deletes
   the file and reports "the file doesn't match the listing"; nothing is
   unpacked.
4. **Unpack** into the staging folder. The archive must contain exactly one
   top-level folder named after the title ID. Rejected: absolute paths, `..`,
   symbolic links, names outside that folder, an unpacked size beyond a limit,
   a `sce_sys/param.json` that is missing or names another `titleId`.
5. **Put in place** with one rename from staging to
   `<location>/<TITLEID>`. Staging is on the same filesystem as the location
   so the rename is a single step.
6. **Record** a receipt (section 6.5) and show: "Installed. ShadowMountPlus
   will add it to your home screen in a moment."

If the unpacked app's `contentVersion` differs from the catalog's
`content_version`, the install still succeeds and the receipt records what is
actually on disk.

### 5.4 Update

- Offered when the store manages the app and the catalog's `content_version`
  is higher than the installed `contentVersion` (section 3.4).
- Refused while the app is running (section 5.7), with a message to close it.
- Same steps as an install up to "put in place", which becomes: rename the
  current folder to a backup outside the scanned locations, rename the new
  folder in, then delete the backup. If the second rename fails, the backup is
  renamed back.
- The app's saved data is not touched.
- **Update all** queues every available update and skips the running ones,
  listing them at the end.

### 5.5 Uninstall

- Only for apps the store manages.
- Refused while the app is running.
- A confirmation that names the app and says its saved data stays (D15).
- Renames the folder out of the scanned location, then deletes it, then
  removes the receipt. A deletion interrupted half-way leaves nothing in the
  scanned location.
- ShadowMountPlus and the console may keep showing the title's tile until
  they notice; the store says so.

### 5.6 Installed

- At start and after every change, the store looks in each install location
  for folders with a `sce_sys/param.json` and reads `titleId`, `titleName` and
  `contentVersion` from it.
- A folder with a receipt that matches is **managed**. Anything else is
  **not managed**: shown with its name, version and location and the note
  "Installed outside ProsperoStore. Not managed by this app." No actions.
- Image files found in a location are listed as not managed too.
- A receipt whose folder is gone is dropped (the user removed the app by
  hand).

### 5.7 Running-app guard

- Before an update or uninstall starts, and again immediately before the
  folder is touched, the store asks the system whether that title is running.
- The call to use is supplied by the owner (D9). Known so far: the folder
  `/mnt/sandbox/<TITLEID>_000` exists only while the title runs **[proven]**
  from outside the console; whether an elevated app can rely on it is
  **[open]**, so it is at most a second check.
- If the answer can't be obtained, the store treats the app as running and
  refuses.

### 5.8 Updating the store itself

- The store is a catalog app like any other, so the Updates screen shows its
  own update when `versions.json` has a higher `content_version` for
  PPSA99000 than the running build.
- Download, verify and unpack as for any app. The new version is fully staged
  before anything is replaced.
- Replacing the running store's own folder is **[open]** (milestone M6). The
  preferred method is the same two renames as an update, done while the store
  runs, followed by "Restart ProsperoStore to finish the update". If that
  proves unsafe on a console, the store instead shows that a new version
  exists and how to update it by hand (D10).
- The running-app guard never lets the store update itself through the normal
  path; self-update is its own, separately tested path.

### 5.9 States

| App state | Primary action | Notes |
| --- | --- | --- |
| Not installed, ZIP | Install | |
| Not installed, image | none | "Can't be installed by this version of ProsperoStore" |
| Coming soon | none | Reservation |
| Managed, up to date | Uninstall | |
| Managed, update available | Update | Uninstall as second action |
| Managed, version unknown | Uninstall | "This app doesn't publish a comparable version" |
| Not managed | none | The note from 5.6 |
| Running | actions disabled | "Close the app first" |
| In the queue / downloading / verifying / unpacking | Cancel | |
| Failed | Retry | With the reason |

Failure reasons shown to the user: no network; the catalog can't be reached;
the download failed; the file doesn't match the listing; the archive isn't a
valid app; not enough space; the location isn't available; the app is running;
no permission to write (elevation missing).

### 5.10 Settings

- **Install location**: the locations from 3.3 that exist and can be written,
  each with its free space. Default `/data/homebrew`. Changing it affects new
  installs only; an update stays where the app is.
- Check for updates at start: on by default.
- Language (follows the system; can be forced), theme, sound and vibration.
- About: version, the catalog's build, licences, and a storage summary.

---

## 6. Architecture

### 6.1 Repository layout

```text
src/app/          shell, navigation, screens
src/catalog/      API client, models, version comparison, on-disk cache
src/net/          HTTP transport (sceHttp on console, a host implementation for tests)
src/install/      queue, download, verify, unpack, transaction, journal, receipts
src/system/       elevation, install locations, free space, running check, installed scan
src/ui/           taken from ps5-homebrew-ui: components, themes, sound
platform/ps5/     console implementations
platform/host/    PC implementations for development and tests
payload/          the elevation helper, built for PPSA99000
tests/            host tests and fixtures (saved API responses, sample archives)
tools/            build, packaging, host snapshots, console scripts
sce_sys/          param.json, icon and home-screen art
docs/             user guide, architecture notes
```

### 6.2 Runtime model

| Thread | Does | Never does |
| --- | --- | --- |
| Main (render) | Input, UI, drawing | Network or disk waits |
| Network | Catalog and icon requests | Touch the UI |
| Installer | One job at a time: download, hash, unpack, transaction | Touch the UI |
| Disk | Icon decode, cache reads and writes | |

Workers report through a queue the main thread drains once per frame. The
elevation request runs before any of these threads exists (3.1).

### 6.3 Catalog client

- Reads `index.json` for lists, `apps/<TITLEID>.json` for one app, and
  `versions.json` for the update check.
- Tolerant by rule: ignores unknown fields, accepts `null` wherever the API
  allows it, and refuses to act on a `schema` it can't read rather than
  guessing.
- Version comparison is one function with its own tests, implementing the
  table in the API specification.

### 6.4 The install transaction

Paths, for a location `L` (for example `/data/homebrew`) on a drive whose top
folder is `R` (`/data`):

```text
R/prosperostore/staging/<TITLEID>.zip        download
R/prosperostore/staging/<TITLEID>/           unpacked
R/prosperostore/backup/<TITLEID>/            previous version during an update
L/<TITLEID>/                                 the installed app
```

Every step that changes `L` is first written to a journal
(`/data/prosperostore/journal.json`): intent, paths, state. At start the store
reads the journal and finishes or undoes whatever was interrupted:

| Found at start | Action |
| --- | --- |
| Download or unpack in progress | Delete the staging files |
| Old folder moved to backup, new one not in place | Move the backup back |
| New folder in place, backup still there | Delete the backup, write the receipt |
| Uninstall started | Finish deleting |

The rule the transaction keeps: at every instant, `L/<TITLEID>` is either the
complete old version, the complete new version, or absent. Never a mix.

### 6.5 Receipts

`/data/prosperostore/receipts/<TITLEID>.json`: title ID, location, the
`contentVersion` and release tag installed, the artifact's `sha256`, and when.
A receipt is what makes an app managed. It is written last in an install and
removed last in an uninstall.

### 6.6 Interface

- Start from Homebrew UI Lab's `store` design and component library (lists,
  grids, dialogs, forms, progress, toasts), on the kit's 1920 x 1080 virtual
  canvas rendered at the display's resolution.
- Screens: Browse, App page, Installed, Updates, Queue, Settings, and the
  dialogs (confirm, error, restart after self-update, read-only mode).
- Controls follow the kit: D-pad and left stick move, Cross confirms, Circle
  goes back, Triangle opens search, Square opens the queue, Options opens
  settings, L1/R1 switch sections.
- Motion, sound and vibration come from the kit's themes. Every wait has a
  visible state; nothing freezes the picture.
- Budget: 16.67 ms per frame at 4K with a full grid on screen, measured the
  way the kit's console validation measures it.

---

## 7. Work the catalog side owes this plan

Done by the catalog's automation, not by the store:

| Item | Why the store needs it |
| --- | --- |
| Release notes text in `apps/<TITLEID>.json` | "What's new" on the app page without leaving the store |
| A lighter 256-pixel icon and a cache of converted icons in the build | Faster first load of the grid, and catalog builds that stay within their time limit as it grows |
| The store's own listing, with a `param.json` in its repository | Self-update depends on a correct `content_version` for PPSA99000 |
| Developers raising `contentVersion` | Without it their apps never show an update; the catalog already warns them |

---

## 8. Milestones

Each ends with something that can be shown. Hardware gates are marked.

| # | Milestone | Done when |
| --- | --- | --- |
| M0 | Bootstrap | The repository builds an empty title for PPSA99000 from the boilerplate with the OpenGL SDK and the UI kit, on the console build and on the PC host. |
| M1 | **Gate: filesystem** | On a console, after elevation, the store creates, renames and deletes a test folder in `/data/homebrew`, and reports for each other location whether it exists and can be written. Read-only mode appears when elevation is refused. |
| M2 | **Gate: network** | On a console, the store fetches `index.json` from homebrew.page with certificate checks, and downloads one real release from GitHub through the redirect with a correct SHA-256. If `sceHttp` can't, the curl fallback is built here. |
| M3 | Catalog and browse | The full catalog scrolls at 60 frames per second with icons, sections, search and sort, from live data and from the offline cache. PC host first, then console. |
| M4 | Install | A real catalog app installs from the store and appears on the home screen. Every refusal and failure in 5.3 has a test. |
| M5 | Installed, update, uninstall, running guard | The Installed and Updates screens are correct for managed and unmanaged apps; update and uninstall work; a running app is refused. Needs the owner's running check (D9). |
| M6 | **Gate: self-update** | Either the store replaces itself safely and asks for a restart, or the manual path is in place. |
| M7 | Recovery | Power loss is simulated at every step of install, update and uninstall on the PC host, and the journal brings the folder back to a complete state each time. A subset is repeated on a console with a test title. |
| M8 | Polish and release | Languages, sound, art, the user guide, the settings screen, a soak test, the first tagged release, and the catalog listing. |

---

## 9. Testing

- **On the PC host:** everything that isn't the console. The API client
  against saved responses of the live API; the version table; archive
  validation against hostile archives (path escapes, links, wrong title,
  oversized); the transaction with a failure injected at every step; screen
  snapshots of every state in 5.9.
- **On the console:** only what the host can't answer: the gates M1, M2 and
  M6, real installs, the running check, frame time. Every console run follows
  `ps5-agent-runbook` and needs the owner's go-ahead.
- **Never against a real install.** Destructive tests use dedicated test title
  IDs and their own folders.

---

## 10. Risks

| Risk | Effect | Answer |
| --- | --- | --- |
| Elevation unavailable on a user's setup | Nothing can be installed | Read-only mode with a clear explanation; requirements documented |
| A crash or power loss during an install | A half-written app | Staging outside scanned folders, single-rename activation, journal recovery |
| Replacing a running app's files | Console instability | The running guard; the store's own update handled separately |
| Developers not raising `contentVersion` | Updates never appear for their apps | Shown honestly as "no comparable version"; the catalog warns the developer |
| A developer replaces a release file | The download no longer matches | Refused by the hash check; the catalog's health check flags the listing |
| External or USB drive removed during an install | Failed install | Location checked before each step; the journal cleans up at the next start |
| GitHub or homebrew.page unreachable | No installs | Offline browsing from the cache; clear messages; retry |
| Two copies of one title | ShadowMountPlus "duplicate titleId" | Refuse to install what is already present in any location |

---

## 11. Open questions for the owner

1. **Running check (D9):** which call reports that a title is running, and
   does it need anything beyond the filesystem elevation?
2. **Saved data on uninstall (D15):** keep it, as planned, or offer "also
   delete this app's data"?
3. **Pre-releases (D17):** offer them like any release, or add a "stable only"
   setting once the catalog can tell the store what the last stable release
   was?
4. **Unmanaged apps:** should the page of an app installed by hand still show
   that the catalog has a newer version, with no action, or show nothing?
5. **Languages** for the first release: the seven ProsperoEden has, or English
   first?
6. **Design:** start from the kit's `store` design as it is, or a variant that
   matches the website's Holo look?

---

## Appendix A: API calls the store makes

| When | Request |
| --- | --- |
| Start, and on refresh | `GET /api/v1/index.json` |
| Start, and on refresh | `GET /api/v1/versions.json` |
| Opening an app | `GET /api/v1/apps/<TITLEID>.json` |
| A tile or page needs an icon it doesn't have, or its `icon_hash` changed | `GET` the `icon_small` or `icon` address |
| Install or update | `GET` the app's `artifact_url` |

All with `If-None-Match` where a copy is held. The store sends a `User-Agent`
naming itself and its version.

## Appendix B: Where things are on the console

| Path | Holds |
| --- | --- |
| `/data/homebrew/PPSA99000/` | The store itself |
| `/data/prosperostore/settings.json` | Settings |
| `/data/prosperostore/receipts/` | One receipt per managed app |
| `/data/prosperostore/journal.json` | The transaction in progress, if any |
| `/data/prosperostore/cache/` | The last catalog files and the icons |
| `<drive>/prosperostore/staging/`, `backup/` | Work folders, one set per drive |
