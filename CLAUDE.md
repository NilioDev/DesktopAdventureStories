# Notes for Claude

See [CONTRIBUTING.md](CONTRIBUTING.md) for the clean-room rules; they always apply.

## Keep the README up to date

`README.md` is the project's front page for players. It must always match
the latest release and `main`. This is a standing rule:

- **In every PR**, if the change is something a player notices (a feature,
  control, option, supported system, package or file name, install step,
  game-file handling, mods or editor), update `README.md` in the same PR.
  Build or developer changes go in `docs/building.md` instead.
- **Before every release** (a new `v*` tag), read the README against the
  changes since the previous tag (`git log <previous tag>..main`) and fix
  anything that is out of date in a PR first. Check at least: the feature
  list, the download table and package names (they must match
  `docs/releases.md` and `.github/workflows/release.yml`), the steps per
  system, the controls table, the status note and the FAQ.
- Don't put version numbers in the README. Download links go to
  `releases/latest` and the release badge updates itself.
- Never add screenshots or anything else made from the original games;
  the banner in `docs/images/` is our own art.
