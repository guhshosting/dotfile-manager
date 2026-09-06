# Architecture

This document describes how dotfile-manager is put together internally:
module boundaries, data flow, and the safety invariants that every module is
expected to uphold. It is aimed at contributors and reviewers, not end
users — see [README.md](../README.md) for usage.

## Design goals, ranked

1. **Never destroy user data by accident.** Every other goal is subordinate
   to this one. When in doubt, a module refuses and reports an error rather
   than guessing.
2. **Predictable, bounded behavior.** No command's blast radius grows
   silently (e.g. discovery never becomes a full home-directory walk;
   `remove` never becomes a recursive delete).
3. **Testability.** Every module that touches the filesystem is designed to
   be exercised against a temporary directory tree, never the real `$HOME`.
4. **Small, auditable surface.** No GUI/TUI, no network access, no plugin
   system, one static binary, one JSON config schema.

## Module map

```
main.cpp
  └─ cli.cpp            argument parsing, dispatch table, global options
       └─ commands.cpp  one function per subcommand (cmd_discover, cmd_add, ...)
            ├─ discovery.cpp   read-only, bounded dotfile discovery + sensitivity classification
            ├─ config.cpp      JSON (de)serialization, schema validation, XDG paths
            ├─ compare.cpp     status computation (IN_SYNC / MODIFIED / ... / CONFLICT)
            ├─ backup.cpp      the sync engine: home<->store copy/symlink, snapshots, safety backups
            ├─ diff_engine.cpp line-oriented diff + binary detection
            ├─ output.cpp      text / json / markdown rendering, tables
            └─ path_safety.cpp   <-- used by ALL of the above; see below
                 ├─ file_types.cpp   non-throwing stat()-style helpers (kind, special-file detection)
                 ├─ ignore_rules.cpp default + user ignore-pattern matching
                 ├─ atomic_io.cpp    atomic file/dir copy, atomic config writes (temp + fsync + rename)
                 ├─ hashing.cpp      SHA-256 content hashing (sha256.cpp: self-contained implementation)
                 └─ env.cpp          $HOME / $XDG_* resolution
```

Dependency direction is strictly top-to-bottom in this list — `path_safety`,
`file_types`, `ignore_rules`, `atomic_io`, `hashing`, `env`, and `sha256` have
no dependencies on anything above them, so they can (and are) unit-tested in
complete isolation.

`dfm_core` is built as a static library; `main.cpp` links it into the
`dotfile-manager` executable, and the same library is linked into the test
binary so unit tests call the real production code, not a reimplementation.

## Data flow for a typical command

`apply .bashrc --force` as an example:

1. `main.cpp` calls `cli::run(argc, argv)`.
2. `cli.cpp` tokenizes arguments, validates the command name, parses global
   and command-specific flags into a `ParsedArgs`, and rejects unknown
   flags or a `--dry-run`/`--force` combination the command doesn't support
   (`command_accepts_dry_run` / `command_accepts_force` tables) — this is
   the exit-code-2 path.
3. `dispatch()` builds an `AppContext`: resolves `$HOME`, the config path,
   and the store path via `env.cpp`; loads and validates the config via
   `config.cpp` (malformed config → exit 3, config left untouched).
4. `dispatch()` calls `cmd_apply(ctx, args)` in `commands.cpp`.
5. `cmd_apply` resolves which entries are in scope (`select_entries`),
   then for each entry calls into `backup.cpp`'s `sync_entry_store_to_home`.
6. `sync_entry_store_to_home` calls `path_safety::resolve_within()` for
   both the store-side and home-side paths — this is the point where
   traversal, symlink-escape, special-file, and loop checks happen. Any
   failure returns `RefusedPathSafety` (a `SyncOutcome`) without touching
   the filesystem.
7. If a real change is needed and the target already differs, a safety
   backup of the *current* home content is written under
   `<store>/.dotfile-manager/safety-backups/<timestamp>/` via
   `atomic_io.cpp` before anything is overwritten.
8. The actual copy/symlink-replace happens through `atomic_io.cpp`
   (temp-file-then-rename for regular files; explicit special-file
   rejection; no `remove_all`, ever).
9. `cmd_apply` aggregates per-entry `SyncOutcome`s into a `CommandResult`,
   which `output.cpp` renders in the requested format, and `cli.cpp` maps
   the aggregated result to one of the eight fixed process exit codes.

Every subcommand follows this same shape: parse → load context → resolve
scope → delegate to a focused engine module → render → exit code.

## The path safety layer

`path_safety.hpp`/`.cpp` is the single chokepoint every filesystem-touching
code path is required to go through — this is enforced by convention and
by unit tests that specifically probe for naive concatenation bugs
(`../` sequences, absolute-path injection, embedded NUL bytes, symlink
escapes, filesystem loops, and "intermediate component is actually a
regular file" cases).

Two entry points matter:

- `validate_relative_path(rel)` — pure, filesystem-free validation of a
  string as it would appear in `config.json` (`home_path`/`store_path`).
  Rejects empty strings, absolute paths, `..` components, and NUL bytes.
- `resolve_within(base_dir, relative)` — the filesystem-aware check used
  right before any read/write. It requires `base_dir` to already exist and
  canonicalize (callers must create it first via
  `ensure_directory_canonical(..., create_if_missing=true)` — the sync
  functions in `backup.cpp` deliberately do *not* do this themselves, so
  that directory creation is always an explicit, auditable step taken by
  the command layer, not a side effect buried in the sync engine).
  `resolve_within` then verifies the resolved path is still lexically
  and canonically inside `base_dir`, rejects intermediate path components
  that are regular files instead of directories, detects symlink escapes,
  and detects filesystem loops.

Known, deliberately-documented limitation: like effectively every
filesystem-manipulating CLI tool, dotfile-manager cannot provide hard
time-of-check-to-time-of-use (TOCTOU) guarantees against a concurrently
running adversarial process. The safety layer minimizes the window (no
symlink-following, re-verification immediately before mutation) but does
not claim atomicity across the check-then-act boundary that the OS itself
does not provide. This is called out explicitly rather than implied away.

## Status computation

`compare.cpp` is a pure function of (home path, store path, mode,
ignore rules) → `EntryStatus`. It never mutates anything. Directory
comparison walks both trees (bounded by the same ignore rules used
elsewhere) and reports a status per leaf as well as an aggregated status
for the entry. Binary vs. text detection and line diffing live in
`diff_engine.cpp` and are only invoked by `diff`/`status --verbose`, never
by the sync engine (sync engines only need "identical or not", via
`hashing.cpp`, not a rendered diff).

## Configuration

`config.cpp` owns JSON (de)serialization (via nlohmann/json),
schema/structural validation (`validate_config`), and atomic persistence
(`save_config_file`, built on `atomic_io.cpp`'s temp-file + fsync + rename
primitive, so a crash mid-write can never leave a half-written config).
Malformed JSON or a structurally invalid schema causes `load_config_file`
to return `ok=false` without ever writing to the file — every command that
loads config checks this and exits 3 immediately rather than trying to
"repair" or partially proceed.

## Testing strategy

- **Unit tests** (`tests/unit/`, doctest) exercise every module in
  isolation, each against a fresh temporary directory created and torn
  down per test (`FakeHome` helper in `tests/unit/test_helpers.hpp`).
  Coverage includes config parsing/validation, path normalization and every
  rejection category above, hashing, binary detection, diffing, ignore
  pattern matching, sensitive-path classification, atomic writes, and each
  command handler's success/dry-run/force/conflict/error branches.
- **Integration tests** (`tests/integration/run_integration_tests.sh`) run
  the actual compiled binary end-to-end against `mktemp -d` fake home
  directories (`HOME=/tmp/dfm-it.XXXXXX/home`), covering full command
  sequences (`discover` → `add` → `status` → edit → `backup` → `apply`) and
  asserting on both output and exit codes. Neither suite ever reads,
  writes, or lists the developer's real `$HOME`.
- Both suites are wired into CTest (`ctest --test-dir build`) and run in
  both Debug and Release configurations in CI.

## What the tool deliberately does not do

- No network access, no telemetry, no auto-update.
- No merge algorithm — conflicting changes are surfaced (`CONFLICT` status,
  exit 5) and left for the user to resolve by hand.
- No implicit symlink conversion — see README "Management modes".
- No recursive force-delete — production code (`src/`, `include/`,
  `main.cpp`) contains zero calls to `std::filesystem::remove_all`; every
  deletion is a single-item `std::filesystem::remove`, and a non-empty
  directory is refused rather than force-deleted (the only `remove_all`
  call in the whole repository is in the *test harness's* `TempDir`
  cleanup helper, which only ever operates on a `mktemp`-created scratch
  directory, never on user or store data).
