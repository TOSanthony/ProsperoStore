# ProsperoStore

A native PS5 app store for the homebrew catalog at
[homebrew.page](https://homebrew.page): browse, install, uninstall and update
apps with the controller. Title ID `PPSA99000`.

Nothing is built yet. **[PLAN.md](PLAN.md)** is the implementation plan: scope,
what is known about the console and how well, the owner's decisions, the
design, the milestones and the open questions.

The catalog side is ready: the store reads the API at
`https://homebrew.page/api/v1/`, specified in
[the catalog's `docs/api.md`](https://github.com/blackbearreloaded/ps5-homebrew-catalog/blob/main/docs/api.md).
