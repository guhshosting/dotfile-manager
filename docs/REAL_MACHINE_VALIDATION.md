# Real-machine validation checklist

Status: **AUTOMATED/SANDBOX VALIDATION COMPLETE — REAL-MACHINE VALIDATION PENDING.**

Everything in this project has so far been built and tested only inside an
isolated sandbox, against temporary/fake home directories under `/tmp`.
It has never touched a real user's actual `$HOME`, real dotfiles, or a
persistent Debian/Fedora installation. This checklist is the procedure to
run once, by hand, on a real machine (a spare VM or container is strongly
recommended for the first pass) before treating dotfile-manager as
release-ready.

**Do not run any command in Phase 2+ against your real `$HOME` or your
real `~/dotfiles` store.** Every mutating step below uses a dedicated,
disposable test home (`~/dotfile-manager-test-home`) and test store
(`~/dotfile-manager-test-store`) that you create for this purpose and can
delete afterward. Read-only commands (`discover`, `status`, `check`,
`list`, `version`) are safe to run against your real environment and are
called out explicitly as such.

## Phase 0 — build and install

```sh
git clone <repo-url> dotfile-manager   # or unpack the release source archive
cd dotfile-manager
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

- [ ] Build completes with zero warnings.
- [ ] `ctest` reports 100% tests passed (unit + integration).
- [ ] `./build/dotfile-manager version` prints `dotfile-manager 1.0.0`.

Optionally install system-wide and confirm the man page:

```sh
sudo cmake --install build
man dotfile-manager
dotfile-manager --help
```

- [ ] `man dotfile-manager` renders without groff errors and matches the
      command reference in `man/dotfile-manager.1`.

## Phase 1 — read-only checks against your REAL home directory

These commands never write anything. Safe to run as-is.

```sh
dotfile-manager discover
dotfile-manager discover --format json
dotfile-manager list
dotfile-manager status
dotfile-manager check
```

- [ ] `discover` output only lists direct children of `$HOME` and
      `$HOME/.config` — confirm nothing from `~/.cache`, `~/.local/share`,
      browser profile directories, Trash, or Downloads appears.
- [ ] Any SSH/GnuPG/AWS/GCP/password-store paths that exist on your
      machine are shown marked **sensitive**, and their contents are
      never printed.
- [ ] `discover --force` is rejected with a usage error (exit code 2) —
      confirm with `echo $?` after running it.
- [ ] `list`/`status`/`check` against an empty (fresh) config behave
      sanely (empty list, no crash) since nothing has been added yet.

## Phase 2 — full workflow against a DISPOSABLE test home

Set up an isolated, throwaway environment — never your real `$HOME`:

```sh
mkdir -p ~/dotfile-manager-test-home ~/dotfile-manager-test-store
cd ~/dotfile-manager-test-home
export HOME=~/dotfile-manager-test-home
echo 'echo hello from test bashrc' > .bashrc
mkdir -p .config/htop
echo 'sort_key=PERCENT_CPU' > .config/htop/htoprc
```

Run every command below with this `HOME` still exported, and always
`--dry-run` first before the equivalent real invocation. (The store
directory is not a CLI flag — it comes from the config file, defaulting
to `$HOME/dotfiles`, which under this disposable test `HOME` resolves to
`~/dotfile-manager-test-home/dotfiles` — still fully disposable.)

```sh
dotfile-manager discover

dotfile-manager add .bashrc
dotfile-manager status
dotfile-manager diff .bashrc     # expect "No differences."

echo 'echo an edited line' >> .bashrc
dotfile-manager status           # expect MODIFIED
dotfile-manager diff .bashrc     # expect a one-line addition

dotfile-manager backup --dry-run
dotfile-manager backup
dotfile-manager status           # expect IN_SYNC

# add a symlink-mode entry and confirm add never auto-converts it:
echo 'set number' > .vimrc
dotfile-manager add .vimrc --mode symlink
file .vimrc                      # expect: regular ASCII text, NOT a symlink
dotfile-manager status           # expect SYMLINK_MISMATCH for ~/.vimrc
dotfile-manager apply --dry-run  # previews the symlink replacement
dotfile-manager apply --force    # only now does it become a symlink
file .vimrc                      # expect: symbolic link -> the store copy
dotfile-manager status           # expect IN_SYNC

dotfile-manager add .config/htop
dotfile-manager list --format markdown

dotfile-manager backup --snapshot
dotfile-manager list --snapshots

dotfile-manager remove .bashrc
dotfile-manager status   # .bashrc should no longer be listed
test -f .bashrc && echo "OK: home file still exists after remove"

dotfile-manager remove .config/htop --purge-store
# expect a note that the store copy is a non-empty directory and was
# NOT deleted (dotfile-manager never deletes recursively)

dotfile-manager report --format json
```

Note on conflicts: dotfile-manager does not perform three-way merges.
`CONFLICT` status specifically means a path-safety failure or a special
file (socket/FIFO/device) sits where a managed entry expects a regular
file or directory — not "both sides were edited" (that case is reported
as plain `MODIFIED`/`SYMLINK_MISMATCH` and resolved by explicitly running
`backup` for home→store or `apply` for store→home, whichever direction
you want to win). To see a genuine `CONFLICT`/exit-5 case, reuse one of
the special-file probes in Phase 3 on a path that is already a managed
entry.

- [ ] Every mutating command was previewed with `--dry-run` first and the
      preview output matched what actually happened afterward.
- [ ] `add --mode symlink` copies into the store but does **not** turn the
      home file into a symlink until `apply --force` is run explicitly —
      confirmed above with `file .vimrc` before and after.
- [ ] `apply --force` on the symlink-mode entry created a safety backup
      under `<store>/.dotfile-manager/safety-backups/` before replacing
      the home file (check the path printed in the command's own output).
- [ ] `remove` never deletes the home file; `remove --purge-store` deletes
      only the store copy, and refuses (rather than recursively deleting)
      when the store copy is a non-empty directory.
- [ ] Exit codes observed via `echo $?` after each command match the
      table in `README.md`/`man dotfile-manager`.
- [ ] All output formats (`--format text|json|markdown`) render without
      errors or truncation.

## Phase 3 — path-safety adversarial checks (still inside the test home)

```sh
dotfile-manager add '../outside-home' 2>&1; echo "exit: $?"        # expect rejection, exit 4
dotfile-manager add '/etc/passwd' 2>&1; echo "exit: $?"             # expect rejection
ln -s /etc some-symlink-dir
dotfile-manager add some-symlink-dir 2>&1; echo "exit: $?"          # expect rejection (escapes base)
mkfifo a-fifo 2>/dev/null && dotfile-manager add a-fifo 2>&1; echo "exit: $?"  # expect rejection, special file
```

- [ ] All four adversarial cases are rejected with a path-safety error
      (exit code 4) and no files are created or modified as a result.

## Phase 4 — packaging install/uninstall (use a VM or container, not your host)

```sh
# Debian/Ubuntu
cd packaging/debian && ./build-deb.sh
sudo dpkg -i ../dotfile-manager_1.0.0_amd64.deb
dotfile-manager version
man dotfile-manager
sudo dpkg -r dotfile-manager

# Fedora/RHEL
cd packaging/rpm && ./build-rpm.sh
sudo rpm -i ~/rpmbuild/RPMS/x86_64/dotfile-manager-1.0.0-1.*.rpm
dotfile-manager version
man dotfile-manager
sudo rpm -e dotfile-manager
```

- [ ] Package installs and uninstalls cleanly with no leftover files
      outside the package manager's own tracking (verify with
      `dpkg -L dotfile-manager` / `rpm -ql dotfile-manager` before removal,
      then confirm those exact paths are gone after removal).
- [ ] Neither install nor uninstall touches any user's `~/.config` or
      `~/dotfiles`.

## Phase 5 — cleanup

```sh
unset HOME   # restore your real shell's HOME in this terminal, or just close it
rm -rf ~/dotfile-manager-test-home ~/dotfile-manager-test-store
```

- [ ] Confirm your real `$HOME` and real dotfiles were never touched by
      any command above (nothing in this checklist should have written
      to your real home at any point given `HOME` was overridden for all
      of Phase 2–3).

## Sign-off

Once every box above is checked on a real Debian/Ubuntu machine (and
ideally also a Fedora/RHEL machine for the RPM path), update the status
line at the top of this document to:

```
REAL-MACHINE VALIDATION COMPLETE — <date>, <OS/version>, <who ran it>
```

Do not consider dotfile-manager release-ready before that line is
updated with a real completion record.
