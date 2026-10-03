# ProsperoStore

A native PS5 app store for the homebrew catalog at
[homebrew.page](https://homebrew.page): browse, install, uninstall and update
apps with the controller. Title ID `PPSA99000`.

Version 01.000.000 is the first release, a beta. Browsing, installing, updating
and uninstalling have run on a console (firmware as listed in the notes).
**[PLAN.md](PLAN.md)** is the implementation plan: scope, what is known about
the console and how well, the owner's decisions, the design, the milestones and
the open questions.

## Install

1. The console needs ShadowMountPlus and a payload loader listening on port
   9021 (the store uses it to leave its sandbox and to start its file worker).
2. Unpack `PPSA99000.zip` and copy the `PPSA99000` folder into a location
   ShadowMountPlus scans, for example `/data/homebrew`.
3. Start ProsperoStore from the home screen. Later versions are installed by
   the store itself.

## Known limits of this version

- ZIP apps only; image files (ffpfsc) are listed but can't be installed.
- The store installs, updates and removes apps; it does not start them.
- After the store updates itself, the console needs a few minutes before it
  starts the new version; until then the store asks to be opened again.
- An update is only offered for apps whose catalog entry lists a content version.
- The home-screen name of an app does not change after an update.
- USB locations are offered but have not carried a real install yet.
- English only.

See [IMPLEMENTATION_NOTES.md](IMPLEMENTATION_NOTES.md) for completed work,
validation results, known limitations, and the remaining implementation work.

The catalog side is ready: the store reads the API at
`https://homebrew.page/api/v1/`, specified in
[the catalog's `docs/api.md`](https://github.com/blackbearreloaded/ps5-homebrew-catalog/blob/main/docs/api.md).

Build in Linux/WSL with `make DEVELOPMENT=1 app`. Run `make test lint` for
host validation and `make host-snapshots` for the UI preview. Release builds
omit development requests by default. `FOUNDATIONS.json` records immutable
foundation sources; `third_party/STORE_SOURCES.json` records vendored libraries.

All persistent store state is under `/data/prosperostore`, using the boilerplate's
elevation helper. This includes settings, cache, receipts, logs, crash reports
and development-control receipts. If elevation is unavailable, browsing uses
memory only and installation remains disabled. Elevated HTTPS uses PacBrew
curl/OpenSSL with certificate checks against the console's CA list.

Browse with the D-pad or left stick, switch sections with L1/R1, and open an
app with Cross. Options opens Settings (install location, sounds, vibration,
the check for a newer store), the Queue and About; Square opens the Queue. Triangle opens system-keyboard search by name or developer;
press R3 to cycle name, newest release, and recently updated sorting. Clear
the search text to show the full section again. Circle goes back or closes
the store. Native keyboard integration still awaits console qualification.

App pages show verified metadata, full-size artwork, and a scrolling article.
Use the D-pad to scroll and Triangle to retry or refresh details. Scan the QR
code for the app's page, release notes, and source repository.

The screens follow the UI library's Storefront design in the Farlight
colours: a featured banner, section chips, a grid of cards and a product page.
When the catalog lists a newer ProsperoStore, a notice appears in the top-right
corner for ten seconds. On an app's page Cross installs, updates or uninstalls it (Square uninstalls
when an update is offered); progress shows on the page, on the app's card and in
the top bar, and Cross cancels. An app that is running can't be updated or
uninstalled until it is closed. Nothing is changed in an app's folder until the
download has been checked against the signed catalog and unpacked.

Installed scans the configured ShadowMountPlus locations without changing them.
Only folders with matching store receipts are managed; duplicate title IDs
and externally installed apps stay unmanaged. Updates lists newer catalog
versions for managed folders. Image files are listed without mounting them.
