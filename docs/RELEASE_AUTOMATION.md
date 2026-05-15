# Release Automation: dev to prod snapshot

`mogmog-0110/MitiruEngineDev` (private) is the working tree where all
development happens. `mogmog-0110/MitiruEngine` (public, MIT) is the engine
face that consumers clone and use. They are kept in sync by
`tools/release/build_release_snapshot.py`, which reads an allowlist, copies
the approved files from the dev tree into a checkout of the prod repo, and
optionally commits and pushes. Running this script locally from your terminal
is the primary and recommended release path. A GitHub Actions workflow
(`.github/workflows/release.yml`) exists as an optional trigger for future
use but is not the active workflow.

## Releasing locally (primary path)

### One-time setup: clone the prod repo

Clone the prod repo as a sibling directory of your dev checkout. The snapshot
script writes directly into this directory.

```bash
# From the directory that contains your MitiruEngineDev checkout:
git clone https://github.com/mogmog-0110/MitiruEngine.git ../MitiruEngine-prod
```

You only need to do this once. Future releases reuse the same checkout.

### Per-release flow

```bash
# 1. Make sure dev main is green and up to date.
git checkout main
git pull

# 2. Tag the release on dev.
git tag v0.1.0

# 3. Build the snapshot into the prod checkout and push prod.
python tools/release/build_release_snapshot.py \
    --tag v0.1.0 \
    --dest ../MitiruEngine-prod \
    --push --remote origin

# 4. Push the tag back to dev so it is visible on dev origin too.
git push origin v0.1.0
```

Step 3 does everything: copies the allowlisted files, applies rewrites, writes
a prod `.gitignore`, commits with `chore(release): snapshot v0.1.0`, and
pushes to the prod repo's `origin`. Step 4 is a separate push so the tag
appears on the dev remote as well (the script only touches the prod checkout).

## What the snapshot script does

`tools/release/build_release_snapshot.py` performs these steps in order:

1. Reads `tools/release/release_allowlist.yaml`. The allowlist declares
   `include` glob patterns (and optional `exclude` overrides) that determine
   which dev-tree files get shipped.
2. Expands the globs against the dev repo root, collecting matched paths.
   Literal (non-glob) entries that do not exist on disk abort the run
   immediately with a clear error before any copy happens.
3. Applies `rewrite` entries. The most important rewrite is
   `tools/release/PROD_README.md -> README.md`, which ensures consumers see a
   public-facing README rather than the internal dev one.
4. Writes a tailored `.gitignore` into the destination, scoped for engine
   consumers (CMake build dirs, IDE folders, `external/cef/`, etc.).
5. Copies every resolved file into the destination directory.
6. If `--push` is set: runs `git add -A`, commits with
   `chore(release): snapshot <tag>` (or `chore(release): snapshot` when
   `--tag` is omitted), and pushes the destination branch to the named remote.

Two additional flags worth knowing:

- `--allowlist <path>` — override the default allowlist path if you need to
  test a custom allowlist without touching the committed one.
- `--repo-root <path>` — override dev repo root detection (the script defaults
  to two levels above its own location, which is correct for normal use).

## First-ever release

The prod repo is empty today. The snapshot script handles this naturally:
a freshly cloned empty repo is recognized as a valid destination when `--push`
is set (it sees only `.git/` in the directory, which it ignores). The script
will commit and push `main` into the previously-empty remote.

Before doing the first real push, verify the allowlist resolves to the right
set of files using `--dry-run`:

```bash
# Verify the snapshot allowlist resolves correctly without writing or pushing:
python tools/release/build_release_snapshot.py \
    --tag v0.1.0 \
    --dest ../MitiruEngine-prod \
    --dry-run
```

`--dry-run` prints the full planned file list and all rewrites, then exits
with code 0 without copying anything or touching the prod repo. Run this
before the first `--push` to confirm the file set looks correct.

## Optional: GitHub Actions trigger

`.github/workflows/release.yml` is kept in the repo for future flexibility. It
wraps the same snapshot script and fires on tag pushes (`refs/tags/v*`) or
manual `workflow_dispatch`.

**The local path above is the recommended workflow. GitHub Actions is provided
for future flexibility and is not currently active.**

To opt in if you decide to use it later:

1. Create a fine-grained PAT at https://github.com/settings/personal-access-tokens/new:
   - Repository access: only `mogmog-0110/MitiruEngine`
   - Repository permissions: **Contents: Read and write**
2. In `mogmog-0110/MitiruEngineDev`: Settings -> Secrets and variables ->
   Actions -> New repository secret, name it `PROD_RELEASE_TOKEN`.
3. Push a tag. The workflow runs automatically, clones the prod repo using the
   PAT, runs the snapshot script with `--push`, and posts a summary with the
   resulting prod commit SHA.

A Deploy Key with write access to the prod repo is an alternative to the PAT
if you prefer SSH.

## Troubleshooting (local execution)

| Symptom | Likely cause | Fix |
|---|---|---|
| `--tag v0.1.0 not found in dev repo` | Tag was not created locally before running the script | Run `git tag -l` to confirm. Create with `git tag v0.1.0` first. |
| Push fails with `non-fast-forward` or `failed to push some refs` | Someone pushed to prod `main` directly, diverging from the snapshot | Clone prod manually, inspect history, decide whether to revert the outside change. Do not force-push. |
| `include entry missing on disk: <path>` | Allowlist references a file that was renamed or deleted in dev | Edit `tools/release/release_allowlist.yaml` to match the current tree, then re-run. |
| Script prints "working tree clean — nothing to commit" | The dev tree at this tag produces a snapshot identical to current prod | Expected. No action needed — the prod repo is already up to date. |
| `dest is non-empty and --push was not passed` | You pointed `--dest` at a non-empty directory without `--push` | Either add `--push` (if `--dest` is the prod repo checkout) or pick an empty path. |

## CSS validation: stylelint in the local build pipeline

Round 18's parallel Hugo theme work shipped a CSS merge that Hugo accepted
silently — a `.showcase-card` selector body was truncated, a comment block
lost its opening `/*`, and a media query ended up with orphan declarations.
The Round 19 reviewer audit caught it manually via `awk` dump.

To prevent that class of bug from re-occurring, `tools/site/build_site.sh`
and `build_site.bat` invoke **stylelint** against the theme CSS before
Hugo renders. Structural defects (empty blocks, mismatched comments,
orphan @import) fail the build immediately.

### Bootstrap (one-time, per developer)

```bash
# Requires Node.js + npm in PATH.
npm install --prefix .tools/stylelint stylelint stylelint-config-standard
```

`.tools/stylelint/` is gitignored alongside the Hugo and Pagefind binaries.

### Running stylelint directly

```bash
.tools/stylelint/node_modules/.bin/stylelint \
  --config tools/site/stylelint.config.cjs \
  --config-basedir .tools/stylelint \
  "site/themes/mitiru-minimal/static/css/**/*.css"
```

The `--config-basedir` argument is required so stylelint can resolve
`stylelint-config-standard` from inside `.tools/stylelint/node_modules/`.

### Pipeline behavior when stylelint is missing

If `.tools/stylelint/node_modules/.bin/stylelint` is absent, `build_site.sh`
and `build_site.bat` print `[stylelint] skipped (not installed; see
docs/RELEASE_AUTOMATION.md)` and continue. Developers without Node installed
can still build the site locally — stylelint is only an additional check,
not a hard prerequisite.

## Git pre-commit hook for stylelint

To run stylelint automatically on every CSS commit, install the project's
git hooks once after cloning:

```bash
bash tools/install-git-hooks.sh
```

This copies `tools/git-hooks/pre-commit` into your local `.git/hooks/`.
The hook scans `git diff --cached` for `.css` changes under
`site/themes/` and runs stylelint against them. It silently skips when
stylelint is not installed (same graceful behavior as the site build).

Bypass with `git commit --no-verify` if you have a justifiable reason.

`tools/install-git-hooks.sh` also installs a `pre-push` hook
(`tools/git-hooks/pre-push`) that re-validates the **entire** theme CSS on
every push, not just the staged files from a single commit. This catches
merge-introduced defects where two individually clean commits combine into
invalid CSS that no per-commit check ever saw. Like the pre-commit hook, it
silently skips when stylelint is not installed. Use `git push --no-verify`
to bypass when needed.

## Allowlist maintenance notes (v13 audit)

`tools/release/release_allowlist.yaml` was audited in 2026-05 against the
dev tree after the Round 16-27 site/tooling work. Outcome:

- **Added** rendered output paths that landed after the original allowlist
  was authored: `docs/favicon-180.png`, `docs/favicon-32.png`, `docs/ogp.png`,
  `docs/examples/**`, `docs/guides/**`, `docs/release-notes/**`,
  `docs/api/**`, `docs/pagefind/**`, `docs/screenshots/**`.
- **Added** two more public-relevant markdown docs:
  `docs/RELEASE_AUTOMATION.md` (so consumers can understand release flow)
  and `docs/cpp-gameplay-api-gaps.md` (gap tracker visible to integrators).
- **Removed** `site/**` from the include list. Consumers receive the
  rendered `docs/` output; they do not need to re-render Hugo. This shrinks
  prod considerably and avoids shipping our theme source.
- **Excluded** `docs/api/*.md` and `docs/api/_index.jsonl` (Hugo build
  intermediates; the rendered `docs/api/<ns>/index.html` is what consumers
  see via Pages).

Dry-run snapshot at audit time: **1068 files** total (mostly
`include/mitiru/**` headers + rendered `docs/` HTML + Pagefind index).

Re-audit when the next round adds new top-level paths under `docs/`,
`site/static/`, or `tools/release/`.

## Files referenced by this doc

- `tools/release/build_release_snapshot.py` (snapshot logic)
- `tools/release/release_allowlist.yaml` (what gets shipped)
- `tools/release/PROD_README.md` (rewritten to `README.md` on prod)
- `.github/workflows/release.yml` (optional Actions trigger — not the active path)
