# Agent guide: dooblr plugins

Read the design maps before you read code; they exist so you do not have to crawl the repo.

1. Read `design/INDEX.md` first, then `design/plugins/<plugin>.md` for the plugin you are working on
   (including dooblr-sub).
   The generated map there lists the version, every parameter (id, range, default), each source
   file with a one-line role, UI components and sizes, the tests, and the build/install commands.
2. Open only the source files the map points you to. Widen the search only if the map does not
   cover what you need.
3. Follow the suite-wide rules in `design/dooblr-design-system.md` (tokens in
   `design/dooblr-design-tokens.json`).
4. When you change behaviour, UX or a design decision, update the **hand-written notes** in
   `design/plugins/<plugin>.md` (outside the AUTO-GENERATED markers) in the same commit.
5. Never edit between the AUTO-GENERATED markers. They are rewritten by
   `design/tools/update-design.py`:
   - by the pre-commit hook in `.githooks/` (enable once per clone:
     `git config core.hooksPath .githooks`), for the plugins a commit touches;
   - by the `design-maps` GitHub workflow after each push to main that touches a plugin.
   To refresh by hand: `python3 design/tools/update-design.py [plugin ...]`;
   `--check` exits 1 if anything is out of date.
6. Stale check: each map records its plugin folder's git tree hash. If
   `git rev-parse --short=12 HEAD:<plugin>` differs, regenerate before trusting the map.
