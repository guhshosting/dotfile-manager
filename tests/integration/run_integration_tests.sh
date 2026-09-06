#!/usr/bin/env bash
# Command-level integration tests for dotfile-manager.
#
# These tests invoke the *built binary* end-to-end. They NEVER touch the
# real developer HOME: every scenario runs inside a freshly created
# temporary directory used as a fake $HOME (and fake $XDG_CONFIG_HOME under
# it), which is removed on exit via the trap below.
#
# Usage: run_integration_tests.sh <path-to-dotfile-manager-binary>
# (or set DFM_BIN in the environment instead of passing an argument)
#
# SPDX-License-Identifier: MIT
set -uo pipefail

BIN="${DFM_BIN:-${1:-}}"
if [[ -z "$BIN" ]]; then
    echo "usage: $0 <path-to-dotfile-manager-binary>  (or set DFM_BIN)" >&2
    exit 2
fi
if [[ ! -x "$BIN" ]]; then
    echo "error: '$BIN' is not an executable file" >&2
    exit 2
fi
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"

TESTS_RUN=0
TESTS_FAILED=0
CURRENT_TEST=""

# All scratch directories created by this script, removed on exit.
SCRATCH_DIRS=()
cleanup() {
    for d in "${SCRATCH_DIRS[@]:-}"; do
        [[ -n "$d" && -d "$d" ]] && rm -rf -- "$d"
    done
}
trap cleanup EXIT

# Guard against ever operating on a real-looking HOME: every fake home this
# script creates lives under a freshly-made mktemp directory, never $HOME.
new_fake_home() {
    local base
    base="$(mktemp -d "${TMPDIR:-/tmp}/dfm-it.XXXXXX")"
    if [[ "$base" != /tmp/* && "$base" != "${TMPDIR:-/tmp}"/* ]]; then
        echo "internal error: refusing suspicious scratch dir: $base" >&2
        exit 7
    fi
    SCRATCH_DIRS+=("$base")
    mkdir -p "$base/home" "$base/home/.config"
    echo "$base"
}

# run_dfm <fake_home_dir> <args...>
# Runs the binary with HOME/XDG_CONFIG_HOME pinned to the fake home, and
# stdout/stderr captured to globals OUT and RC.
# Relative path arguments (e.g. "add .bashrc") are resolved against the
# process's current working directory, matching real-world shell usage
# where a user runs `dotfile-manager add .bashrc` from within $HOME. So we
# run the binary with its CWD pinned to the fake home directory.
run_dfm() {
    local home="$1"; shift
    OUT="$(cd "$home/home" && HOME="$home/home" XDG_CONFIG_HOME="$home/home/.config" "$BIN" "$@" 2>&1)"
    RC=$?
}

begin_test() {
    CURRENT_TEST="$1"
    TESTS_RUN=$((TESTS_RUN + 1))
    echo "--- TEST: $CURRENT_TEST"
}

fail() {
    echo "    FAIL: $1" >&2
    if [[ -n "${OUT:-}" ]]; then
        echo "    --- last command output ---" >&2
        echo "$OUT" | sed 's/^/    | /' >&2
    fi
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

assert_rc() {
    local expected="$1" actual="$2" msg="$3"
    if [[ "$expected" != "$actual" ]]; then
        fail "$msg (expected exit $expected, got $actual)"
        return 1
    fi
    return 0
}

assert_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if [[ "$haystack" != *"$needle"* ]]; then
        fail "$msg (expected output to contain: $needle)"
        return 1
    fi
    return 0
}

# ===========================================================================
# Test 1: discover -> add -> status -> modify -> diff -> backup -> status
# ===========================================================================
begin_test "discover -> add -> status -> modify -> diff -> backup -> status workflow"
FH="$(new_fake_home)"
echo 'export PATH=$PATH:/opt/bin' > "$FH/home/.bashrc"
mkdir -p "$FH/home/.config/nvim"
echo 'set number' > "$FH/home/.config/nvim/init.vim"

run_dfm "$FH" discover
assert_rc 0 "$RC" "discover should succeed"
assert_contains "$OUT" ".bashrc" "discover should list .bashrc"

run_dfm "$FH" add .bashrc
assert_rc 0 "$RC" "add .bashrc should succeed"
[[ -f "$FH/home/dotfiles/.bashrc" ]] || fail "store copy of .bashrc should exist after add"

run_dfm "$FH" status
assert_rc 0 "$RC" "status after add should report in-sync (exit 0)"
assert_contains "$OUT" ".bashrc" "status should mention .bashrc"

# Modify the home copy so it now differs from the store copy.
echo 'export EDITOR=vim' >> "$FH/home/.bashrc"

run_dfm "$FH" status
assert_rc 1 "$RC" "status after modifying home copy should report differences (exit 1)"
assert_contains "$OUT" "MODIFIED" "status should report MODIFIED state"

run_dfm "$FH" diff .bashrc
assert_rc 1 "$RC" "diff should report differences (exit 1)"
assert_contains "$OUT" "EDITOR" "diff should show the added line"

run_dfm "$FH" backup
assert_rc 0 "$RC" "backup should sync home content into the store"
if ! grep -q "EDITOR" "$FH/home/dotfiles/.bashrc"; then
    fail "store copy should contain the new content after backup"
fi

run_dfm "$FH" status
assert_rc 0 "$RC" "status after backup should be back in sync (exit 0)"

# ===========================================================================
# Test 2: copy-mode apply (store -> home) after a home-side modification is
# reverted by re-applying the store's content.
# ===========================================================================
begin_test "copy-mode apply propagates store content back to home"
FH="$(new_fake_home)"
echo 'alias ll="ls -la"' > "$FH/home/.bashrc"
run_dfm "$FH" add .bashrc
assert_rc 0 "$RC" "add .bashrc should succeed"

# Diverge home from the store, then apply to overwrite home from the store.
echo 'alias ll="ls -la"' > "$FH/home/.bashrc"
echo '# local edit not in the store' >> "$FH/home/.bashrc"

run_dfm "$FH" apply .bashrc --force
assert_rc 0 "$RC" "apply --force should succeed and overwrite home from the store"
if grep -q "local edit" "$FH/home/.bashrc"; then
    fail "apply --force should have overwritten the local edit with store content"
fi

# ===========================================================================
# Test 3: symlink-mode add + apply
# ===========================================================================
begin_test "symlink-mode add + apply links home to the store copy"
FH="$(new_fake_home)"
echo 'set -o vi' > "$FH/home/.editrc"
run_dfm "$FH" add .editrc --mode symlink
assert_rc 0 "$RC" "symlink-mode add should succeed"

# add() must NEVER auto-convert the existing regular file into a symlink -
# it only copies content into the store and records the mode. The home
# file is still a plain regular file at this point, so status correctly
# reports a symlink mismatch rather than in-sync.
run_dfm "$FH" status
assert_rc 5 "$RC" "status right after symlink-mode add should report a symlink mismatch (exit 5)"
assert_contains "$OUT" "SYMLINK_MISMATCH" "status should report SYMLINK_MISMATCH before the first apply"
if [[ -L "$FH/home/.editrc" ]]; then
    fail "add must never auto-convert an existing file into a symlink"
fi

# An explicit apply --force is required to actually turn the home path into
# a symlink pointing into the store.
run_dfm "$FH" apply .editrc --force
assert_rc 0 "$RC" "symlink-mode apply --force should succeed"
[[ -L "$FH/home/.editrc" ]] || fail "home path should be a symlink after symlink-mode apply --force"

run_dfm "$FH" status
assert_rc 0 "$RC" "status should report in-sync once the symlink has been applied"

# ===========================================================================
# Test 4: conflict refusal without --force
# ===========================================================================
begin_test "apply refuses a conflicting change without --force"
FH="$(new_fake_home)"
echo 'original' > "$FH/home/.bashrc"
run_dfm "$FH" add .bashrc
assert_rc 0 "$RC" "add .bashrc should succeed"

echo 'store-side change' > "$FH/home/dotfiles/.bashrc"
echo 'home-side change' > "$FH/home/.bashrc"

run_dfm "$FH" apply .bashrc
assert_rc 5 "$RC" "apply without --force on a genuine conflict should exit 5 (unresolved conflict)"
if [[ "$(cat "$FH/home/.bashrc")" != "home-side change" ]]; then
    fail "home file must be left untouched when apply refuses a conflict"
fi

# ===========================================================================
# Test 5: --dry-run leaves the filesystem unchanged
# ===========================================================================
begin_test "--dry-run never mutates the filesystem"
FH="$(new_fake_home)"
echo 'content' > "$FH/home/.bashrc"
run_dfm "$FH" add .bashrc
assert_rc 0 "$RC" "add .bashrc should succeed"

echo 'changed content' > "$FH/home/.bashrc"
before_hash="$(sha256sum "$FH/home/dotfiles/.bashrc" | awk '{print $1}')"

run_dfm "$FH" backup --dry-run
assert_rc 1 "$RC" "dry-run backup with pending changes should exit 1 (preview only)"
assert_contains "$OUT" "DRY RUN" "dry-run output should be clearly labeled"

after_hash="$(sha256sum "$FH/home/dotfiles/.bashrc" | awk '{print $1}')"
if [[ "$before_hash" != "$after_hash" ]]; then
    fail "store copy must not change during a --dry-run backup"
fi

# ===========================================================================
# Test 6: JSON output is well-formed and parses
# ===========================================================================
begin_test "JSON output format produces valid, parseable JSON"
FH="$(new_fake_home)"
echo 'content' > "$FH/home/.bashrc"
run_dfm "$FH" add .bashrc
assert_rc 0 "$RC" "add .bashrc should succeed"

run_dfm "$FH" status --format json
assert_rc 0 "$RC" "status --format json should succeed"
if command -v jq >/dev/null 2>&1; then
    if ! echo "$OUT" | jq . >/dev/null 2>&1; then
        fail "status --format json output failed to parse as JSON"
    fi
elif command -v python3 >/dev/null 2>&1; then
    if ! echo "$OUT" | python3 -c 'import json,sys; json.load(sys.stdin)' >/dev/null 2>&1; then
        fail "status --format json output failed to parse as JSON"
    fi
else
    fail "neither jq nor python3 available to validate JSON output"
fi

# ===========================================================================
# Test 7: a malformed config file is safely rejected, never overwritten
# ===========================================================================
begin_test "malformed config.json is safely rejected and left untouched"
FH="$(new_fake_home)"
mkdir -p "$FH/home/.config/dotfile-manager"
printf '{ this is not valid json' > "$FH/home/.config/dotfile-manager/config.json"
before_content="$(cat "$FH/home/.config/dotfile-manager/config.json")"

run_dfm "$FH" status
assert_rc 3 "$RC" "status with a malformed config should exit 3 (config error)"

after_content="$(cat "$FH/home/.config/dotfile-manager/config.json")"
if [[ "$before_content" != "$after_content" ]]; then
    fail "malformed config.json must never be modified or overwritten"
fi

# ===========================================================================
# Test 8: discover is read-only and rejects --force with a usage error
# ===========================================================================
begin_test "discover rejects --force as a usage error"
FH="$(new_fake_home)"
run_dfm "$FH" discover --force
assert_rc 2 "$RC" "discover --force should be rejected as a CLI usage error (exit 2)"

# ===========================================================================
# Test 9: remove never deletes the actual home file
# ===========================================================================
begin_test "remove never deletes the managed home file by default"
FH="$(new_fake_home)"
echo 'keep me' > "$FH/home/.bashrc"
run_dfm "$FH" add .bashrc
assert_rc 0 "$RC" "add .bashrc should succeed"

run_dfm "$FH" remove .bashrc
assert_rc 0 "$RC" "remove should succeed"
[[ -f "$FH/home/.bashrc" ]] || fail "remove must never delete the actual home file"
[[ "$(cat "$FH/home/.bashrc")" == "keep me" ]] || fail "home file content must be unchanged after remove"

# ===========================================================================
# Summary
# ===========================================================================
echo "==============================================================================="
echo "Integration tests: $TESTS_RUN run, $((TESTS_RUN - TESTS_FAILED)) passed, $TESTS_FAILED failed"
if [[ "$TESTS_FAILED" -ne 0 ]]; then
    exit 1
fi
exit 0
