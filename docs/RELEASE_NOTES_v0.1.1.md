# MitiruEngine Release Notes — v0.1.1

**Date:** 2026-05-18
**Tag:** `v0.1.1`
**Commits since v0.1.0:** 19 (`af71af76..e7609aba` + this RN commit)
**Engine API delta:** none (`include/mitiru/` byte-identical to v0.1.0)
**Test coverage:** ctest 2259/2259 (unchanged from v0.1.0)
**Working tree:** clean

> v0.1.1 is a **front-door release**. No new engine API surface, no behavior
> changes, no migration steps for consumers. What changed is how the engine
> is introduced — to humans on the website, and to people scaffolding their
> first project via the new CLI. The C++ surface in `include/mitiru/` is
> identical to v0.1.0.

---

## TL;DR

- **`mitiru` CLI is now the front door.** All user-facing docs (`README.md`,
  `docs/GETTING_STARTED.md`, `docs/READING_ORDER.md`, the
  `getting-started/` and `tutorials/*` pages on the live site) lead with
  `go install ... && mitiru new && mitiru run`. CMake is demoted to an
  "advanced / 裏で動いてる" section for `FetchContent` / `find_package`
  consumers.
- **The published website was restructured for readability.** Every chapter
  / tutorial / guide page now has the same anatomy: page eyebrow with
  back-link, an optional TLDR "このページで分かること" summary, the body,
  and a sidebar TOC for long pages (>1.5 KB raw content). Plus prev/next
  pagers at the bottom where ordering is well-defined.
- **AI-flavoured chrome was removed.** The hero rainbow underline,
  Konami code easter egg, button shine sweep, cursor blink, and 404 mascot
  bob were all toned down or deleted. The site is now `data-fade` only for
  scroll-in, no other ambient motion.
- **Examples were retired pending redesign.** The previous example set was
  removed in Round 39 because the samples no longer reflected the engine's
  scope. `examples/` is currently a stub directory; a fresh set will land
  in a follow-up release.
- **Release allowlist tightened.** `docs/404.html`, `docs/img/**`,
  `docs/chapters/`, `docs/architecture/` were missing from the public-repo
  release allowlist and are now included. `docs/js/**` was missing too,
  which caused a real production regression (see "Bug fixes" below).

---

## What's not in this release

- **No engine API additions or removals.** `include/mitiru/` is unchanged.
  Any consumer pinned to v0.1.0 can move to v0.1.1 with no source changes.
- **No CMake removal.** `mitiru` CLI is the recommended entry point, but
  `FetchContent_Declare(Mitiru ...)` and `find_package(Mitiru CONFIG)`
  continue to work exactly as before. The architecture page still
  documents CMake 3.21+ as the internal build system because that's the
  truth — `mitiru` CLI just wraps it.
- **No EN localization.** The site is now JA-only by deliberate choice
  (`defaultContentLanguage = "ja"` in `site/config.toml`). Resurrecting
  EN is open for a future release once the JA copy is fully stable.

---

## Front-door migration (Round 42)

The published `README.md` quickstart used to be:

```bash
git clone --recursive https://github.com/mogmog-0110/MitiruEngine.git
cd MitiruEngine
cmake --preset default
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

It is now:

```bash
go install github.com/mogmog-0110/mitiru-cli/cmd/mitiru@latest
mitiru new my-game
cd my-game
mitiru run
```

`mitiru new` generates `src/main.cpp` / `mitiru.toml` / `assets/scene.html`,
`mitiru build` reads `mitiru.toml` and synthesises a `CMakeLists.txt` under
`build/cmake/`, then invokes CMake configure + build behind the scenes.
The CMake recipe is still available in the "CMake から直接使うときは"
section of both `README.md` and `docs/GETTING_STARTED.md`.

Migrated pages:

| File | What changed |
|------|--------------|
| `README.md` | Quickstart, mitiru.toml schema, CMake demoted to "advanced" |
| `docs/GETTING_STARTED.md` | Full rewrite; mitiru CLI flow, `mitiru.toml` reference, troubleshooting table |
| `docs/READING_ORDER.md` | Newcomer paths route through `mitiru new` |
| `site/content/getting-started/_index.md` | Matches README quickstart |
| `site/content/tutorials/01-hello-novel/_index.md` | `mitiru new my-novel` + `mitiru run` |
| `site/content/tutorials/02-action-prototype/_index.md` | `mitiru new my-action` + `mitiru run` |
| `site/content/tutorials/03-save-load/_index.md` | `mitiru new my-save` + `mitiru run` |
| `site/content/guides/gameplay-programming.md` | `mitiru new my-game` + edit `src/main.cpp` |

`site/content/architecture/_index.md` deliberately still names CMake — that
page documents the *internal* build system, not the user-facing entry.

## Site structural overhaul (Round 41)

Every long-form page now follows the same anatomy:

1. **Page eyebrow** — small back-link breadcrumb above the title.
2. **Title + description** — `description` from frontmatter renders as a
   lede line under `<h1>`.
3. **TLDR (optional)** — when frontmatter contains `tldr: [...]`, an aside
   block "このページで分かること" renders before the body.
4. **Body**.
5. **Sidebar TOC (optional)** — for pages with >1500 chars of raw content,
   the table of contents pins to the right column on viewports ≥1024px.
   Collapses inline below 1024px.
6. **Prev/next pager** — when the section has a defined ordering (chapters,
   tutorials), the bottom of the page links to the adjacent siblings.

Touched layouts: `_default/single.html`, `_default/list.html`,
`chapters/single.html`, `guides/single.html`, `architecture/list.html`,
`tutorials/single.html`. New CSS lives in
`site/themes/mitiru-minimal/static/css/main.css` under the `page-eyebrow`,
`page-summary`, `page-with-toc`, `page-toc`, and `page-pager` classes
(~205 lines added).

Naming note: Hugo's reserved `.Summary` shadowed `.Params.summary`, so the
frontmatter key is `tldr:` (not `summary:`) across all migrated pages.

## Site visual cleanup (Rounds 39–41)

- Examples landing page deleted (the 14 visual examples it indexed were
  retired together).
- Home page partials trimmed: `home/position.html`, `home/features.html`,
  and `home/playful-canvas.html` are gone; the home is now just hero +
  3-minutes block + 4-pillars showcase.
- Hero `home-hero__bg` rainbow underline animation removed.
- Konami code listener removed from `site/themes/mitiru-minimal/static/js/`.
- `.btn` shine sweep keyframes removed; buttons are static again.
- 404 mascot `float` animation removed; SVG is static.
- Cursor blink in code samples removed.
- Chapter-list grid: 3-child grid (`::before`, title, desc) was placing
  desc into row 2 col 1 of a 2-column grid → per-character wrapping in the
  2.6rem column. Fixed by explicitly setting `grid-column: 2` on title and
  desc.

## Bug fixes

| Symptom | Root cause | Fix |
|---------|-----------|-----|
| All `[data-fade]` blocks stuck at `opacity: 0` on live site | `docs/js/` was missing from `release_allowlist.yaml` → prod 404'd 5 home JS files → IntersectionObserver never reveals | `docs/js/**` added to allowlist; 1.2 s safety-net timeout added in `scroll-fade.js` so missing JS no longer hides content |
| Architecture page SVG diagrams rendered as solid black blocks | 171 `fill="var(--c-...)"` attributes; SVG presentation attributes don't resolve CSS variables | Python script converted all to `style="fill: var(...); stroke: var(...)"` |
| Architecture page SVG bodies were empty | Blank lines inside SVG blocks caused Goldmark to insert `<p>` after `</defs>`, ejecting the rest from the DOM | Python regex stripped blank lines inside each `.arch-svg` block |
| API page rendered as tall single column | `.api-page` is a 3-column grid (sidebar / content / TOC); the override was also catching the list view | `.api-page--list { display: block; max-width: 1100px; margin: 0 auto; }` |
| Chapter list descriptions wrapped per-character | 3-child grid (`::before`, title, desc) placed desc in row 2 col 1 of `2.6rem 1fr` grid | Explicit `grid-column: 2` on title and desc |
| Frontmatter `summary:` ignored on rendered pages | Hugo's reserved `.Summary` shadowed `.Params.summary` | Renamed frontmatter key to `tldr:` across all migrated pages |

## Tooling

- `mitiru` CLI (separate repo: <https://github.com/mogmog-0110/mitiru-cli>)
  Phase 2 is now reflected in its README. All six subcommands documented:
  `new` / `build` / `run` / `clean` / `doctor` / `version`. `--release`
  and `--config` flags noted for `build` / `run`.
- `tools/release/release_allowlist.yaml`: added `docs/404.html`,
  `docs/img/**`, `docs/js/**`, `docs/chapters/**`, `docs/architecture/**`,
  `docs/getting-started/**`.
- `tools/site/build_site.bat` / `.sh`: full Hugo + Pagefind build script
  runs stylelint first (when installed) and bails on lint failure.
- Site `pagefind` index: 1 language (ja) / 74 pages / 15085 words indexed.

## Going forward

- A fresh `examples/<name>/` set is the next deliverable. The currently
  empty `examples/CMakeLists.txt` will be repopulated with small,
  single-purpose runnable samples in a follow-up release.
- The `mitiru` CLI will gain additional templates beyond the default
  `hello` (Mode B) — `native-only` for Mode A is the next target.
- CEF subprocess `error_code=63` still ships with the `--single-process`
  workaround. CRT mismatch diagnostic continues in
  `docs/CEF_CRT_EXPERIMENT.md`.

## Upgrade path

```bash
# From a v0.1.0 consumer project, no source changes needed:
git pull               # if you're tracking main, the engine moves to e7609aba
# or
GIT_TAG v0.1.1         # if you're pinning by tag in FetchContent_Declare
```

`v0.1.1` is wire-compatible with `v0.1.0` for any consumer that used
`FetchContent` or `find_package(Mitiru CONFIG)`.

---

*Engine API delta from v0.1.0: `include/mitiru/` is byte-identical (verified
via `git diff --stat v0.1.0..HEAD -- include/` = empty).*
