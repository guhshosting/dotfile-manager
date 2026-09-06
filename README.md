# dotfile-manager

A safe, boring, command-line utility for discovering, inventorying, comparing,
backing up, and restoring your dotfiles. It has exactly one job: keep a
managed copy of the config files you choose in a "store" directory (default
`$HOME/dotfiles`), and let you sync changes between that store and your real
`$HOME` without ever surprising you.

- **C++17, no runtime dependencies** besides [nlohmann/json](https://github.com/nlohmann/json) (build-time only).
- **Linux only.** Plain CLI — no GUI, no TUI, no daemon.
- **MIT licensed.**

## Why another dotfile manager?

Most dotfile tools either (a) happily overwrite your files, (b) walk your
entire home directory including caches, browser profiles, and SSH keys, or
(c) quietly turn your files into symlinks you didn't ask for. dotfile-manager
is built around the opposite defaults:

- It **never overwrites an existing file without `--dry-run`/`--force`**.
- It **never scans your whole home directory** — discovery is bounded to
  `$HOME` (one level deep) plus `$HOME/.config` (one level deeper).
- It **never touches** `~/.ssh`, `~/.gnupg`, `~/.aws/credentials`,
  `~/.config/gcloud`, password stores, cookie databases, or other
  credential-shaped paths without an explicit `--allow-sensitive` opt-in —
  and it never prints or parses their contents.
- It **never silently converts a file into a symlink.** Copy vs. symlink is
  a mode you choose per entry, and applying that mode is always an explicit,
  confirmable step.
- `remove` **never deletes your actual file.** It only forgets that
  dotfile-manager was managing it.
- It **never recursively deletes a directory**, even with `--force` — the
  implementation only ever calls the single-item `remove()`, never
  `remove_all()`, so a non-empty directory can never be silently destroyed.

## Installation

### Build from source

Requirements: CMake ≥ 3.16, a C++17 compiler (tested with GCC 14/15), and
the `nlohmann-json` development package (`nlohmann-json3-dev` on
Debian/Ubuntu, `json-devel`/`nlohmann-json-devel` on Fedora/RHEL).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

This installs the `dotfile-manager` binary, the man page, and `README.md`
under standard `GNUInstallDirs` locations (typically `/usr/local`).

### Debian / Ubuntu package

```sh
cd packaging/debian
./build-deb.sh
sudo dpkg -i ../dotfile-manager_1.0.0_amd64.deb
```

### RPM-based distributions

```sh
cd packaging/rpm
./build-rpm.sh
sudo rpm -i ~/rpmbuild/RPMS/x86_64/dotfile-manager-1.0.0-1.*.rpm
```

See `packaging/debian/README.md` and `packaging/rpm/README.md` for details.

## Quick start

```sh
# 1. See what dotfile-manager would consider managing (read-only, no args).
dotfile-manager discover

# 2. Start tracking a file. Copies it into the store ($HOME/dotfiles by default).
dotfile-manager add .bashrc

# 3. Check whether your home copy still matches the store copy.
dotfile-manager status

# 4. See exactly what changed.
dotfile-manager diff .bashrc

# 5. Pull home-side edits into the store.
dotfile-manager backup

# 6. Push store-side content back out to $HOME (e.g. on a new machine).
dotfile-manager apply --force
```

Every mutating command supports `--dry-run` to preview exactly what would
happen without touching anything, and requires `--force` before it will
overwrite or replace existing content.

## Commands

```
dotfile-manager <command> [arguments] [options]

  discover                Find likely dotfiles/config files (read-only)
  list [--snapshots]      List currently managed entries, or snapshots
  add <path> [opts]       Add a path to the managed set (copies into the store)
  remove <path> [opts]    Remove a path from management metadata
  status [paths...]       Show sync status of managed entries
  diff <path>             Show differences between store and home versions
  backup [paths...]       Copy home content into the managed store
  apply [paths...]        Apply managed store content to home
  restore --snapshot-id ID [paths...]
                          Restore home content from a snapshot
  check                   Validate configuration and managed paths
  report                  Produce a complete status/check report
  version                 Print version information
```

Global options: `--format text|json|markdown`, `--output FILE`, `--verbose`,
`--quiet`, `--no-color`, `--dry-run`, `--force`, `--help`/`-h`, `--version`.

Command-specific options:

- `add`: `--mode copy|symlink`, `--store-path PATH`, `--tag NAME` (repeatable), `--allow-sensitive`
- `remove`: `--purge-store` (deletes the store copy too — refuses on non-empty directories, never recursive)
- `backup`: `--snapshot` (creates a timestamped snapshot before syncing)
- `restore`: `--snapshot-id ID|latest` (required)

Run `dotfile-manager --help` or `man dotfile-manager` for the full reference,
and see `docs/ARCHITECTURE.md` for how the pieces fit together.

## Discovery scope, in detail

`discover` is strictly read-only: it takes no positional arguments and
rejects `--force` with a usage error. It looks at:

1. Direct children of `$HOME` whose name starts with `.` (depth 1).
2. Direct children of `$HOME/.config` (one directory deeper than `$HOME`).

It never recurses further, and it never looks inside `~/.cache`,
`~/.local/share`, browser/chat-app profile directories (Chrome, Chromium,
Slack, Discord, Signal, VS Code, etc.), Trash, or Downloads.

Paths are classified **sensitive** — surfaced, but excluded from `add`
unless you pass `--allow-sensitive` — when they match built-in heuristics
covering SSH keys, GnuPG data, AWS/GCP/kube credentials, password stores,
Docker/npm/git credential files, and common secret-shaped filenames
(`id_rsa`, `.netrc`, `known_hosts`, `cookies.sqlite`, anything containing
`credential`/`token`, etc.). dotfile-manager never opens or prints the
contents of a path it has classified as sensitive.

## Management modes

Each managed entry has a mode, set at `add` time and changeable only by
re-adding:

- **copy** (default): the store holds an independent copy of the file/tree.
  `backup` pulls home → store; `apply` pushes store → home. Both directions
  require `--force` to overwrite existing, differing content, and both
  support `--dry-run`.
- **symlink**: the intent is for `$HOME` to eventually contain a symlink
  into the store. `add` never performs this conversion itself — it only
  copies content into the store and records the mode. You must run
  `apply --force` explicitly to replace the home file with a symlink (a
  safety backup of the previous content is made first, under
  `<store>/.dotfile-manager/safety-backups/<timestamp>/`).

## Status states and exit codes

`status`/`diff` report one of: `IN_SYNC`, `MODIFIED`, `MISSING_HOME`,
`MISSING_STORE`, `TYPE_MISMATCH`, `SYMLINK_MISMATCH`, `CONFLICT`, `UNKNOWN`.

Every command exits with one of these fixed codes:

| Code | Meaning |
|------|---------|
| 0 | Success — everything in sync, operation completed cleanly |
| 1 | Differences or non-fatal warnings found |
| 2 | CLI usage error |
| 3 | Configuration error (malformed config; the file is left untouched) |
| 4 | Path-safety error (traversal, symlink escape, special file, etc.) |
| 5 | Unresolved conflict (refused without `--force`) |
| 6 | I/O failure |
| 7 | Internal error |

`apply` and `backup` refuse genuine conflicts by default (exit 5) rather
than guessing which side is "right."

## Configuration file

dotfile-manager keeps its own metadata (never your dotfile *contents*) in a
small JSON file at `$XDG_CONFIG_HOME/dotfile-manager/config.json` (falling
back to `~/.config/dotfile-manager/config.json`):

```json
{
  "store": "/home/you/dotfiles",
  "entries": [
    {
      "home_path": ".bashrc",
      "store_path": ".bashrc",
      "mode": "copy",
      "tags": ["shell"],
      "ignore": []
    }
  ],
  "ignore_patterns": [],
  "use_default_ignores": true,
  "version": 1
}
```

If this file is malformed, dotfile-manager refuses to run any
config-dependent command (exit code 3) and **never overwrites the malformed
file** — fix it by hand or delete it to start fresh.

## Path safety

All path handling goes through a dedicated safety layer, not string
concatenation. It rejects, before touching the filesystem:

- `..` traversal components and absolute paths where a relative path is required
- Paths that resolve outside the declared base directory (home or store)
- Intermediate symlinks that escape the base directory, or that form a
  filesystem loop
- Special files (sockets, FIFOs, block/character devices) — these are never
  copied, only reported
- Malformed, empty, or filesystem-root paths

Known, honestly-documented limitation: like virtually every CLI tool that
operates on a live filesystem, dotfile-manager does not provide hard
TOCTOU (time-of-check-to-time-of-use) guarantees — a sufficiently
adversarial concurrent process could still race a symlink swap between a
safety check and the following filesystem operation. The safety layer
minimizes this window and never blindly follows symlinks, but it does not
claim atomicity it cannot deliver.

## Testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

This runs both the doctest-based unit suite (`dfm_unit_tests`, 300+ tests)
and the command-level integration suite (`dfm_integration_tests`), which
exercises the built binary end-to-end against temporary fake `$HOME`
directories under `/tmp` — it never touches your real home directory.

## Known limitations

- Linux only; not tested on macOS or BSD (relies on Linux-style
  `/proc`-free `std::filesystem` semantics and does not attempt to handle
  macOS resource forks or extended attributes).
- No TOCTOU hardening beyond avoiding symlink-following and re-checking
  types immediately before mutating (see "Path safety" above).
- No merge/three-way-diff support — conflicts must be resolved by hand
  (inspect with `diff`, then explicitly `backup --force` or `apply --force`).
- Snapshots are full-content copies, not incremental; large managed trees
  will produce correspondingly large snapshots.

## License

MIT. See [LICENSE](LICENSE).
