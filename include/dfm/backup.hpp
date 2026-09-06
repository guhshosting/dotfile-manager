// backup.hpp - the sync/backup/apply/snapshot execution engine.
//
// This module performs the only filesystem MUTATIONS in dotfile-manager
// (aside from config persistence). Every function here is dry-run aware and
// enforces the project's hard safety invariants:
//   - never silently overwrite differing content without --force
//   - never recursively delete a non-empty directory, force or not
//     (achieved structurally: only std::filesystem::remove(), which fails
//     on non-empty directories, is ever used - remove_all() is never called
//     on a home/store-derived path)
//   - never follow a destination symlink to write through it; symlinks are
//     replaced atomically at the link path itself
//   - all resolution goes through dfm::resolve_within, so a path that
//     escapes HOME or the store is refused before any I/O happens
//
// SPDX-License-Identifier: MIT
#pragma once

#include "dfm/config.hpp"
#include "dfm/ignore_rules.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dfm {

enum class SyncOutcome {
    NoChangeInSync,
    WouldCreate,
    Created,
    WouldUpdate,
    Updated,
    WouldReplaceSymlink,
    ReplacedSymlink,
    RefusedConflict,
    RefusedTypeMismatch,
    RefusedUnsafeDirectoryReplace,
    RefusedPathSafety,
    Skipped,
    Failed,
};

std::string to_string(SyncOutcome outcome);

// True for outcomes that represent differences still requiring attention
// (used to compute process exit codes).
bool is_refused(SyncOutcome outcome);
bool is_change(SyncOutcome outcome);  // Created/Updated/ReplacedSymlink (i.e. a real mutation happened)

struct SyncResult {
    SyncOutcome outcome = SyncOutcome::Skipped;
    std::string message;
    std::vector<std::string> changed_files;  // relative paths touched (directory entries)
};

// Recursively copies `src_dir` into `dest_dir` (creating it if missing),
// preserving symlinks as symlinks (never followed) and regular file
// permission bits, skipping paths matched by `ignore` and refusing to copy
// special files. Existing regular files at the destination that already
// match by content are left untouched (no spurious rewrites); files that
// differ ARE overwritten, because callers only invoke this after their own
// entry-level conflict policy has already permitted the sync. Never deletes
// anything at the destination (extra destination-only files are left in
// place - see docs/ARCHITECTURE.md "Non-destructive merge" section).
std::optional<std::string> copy_tree(const std::filesystem::path& src_dir, const std::filesystem::path& dest_dir,
                                      const IgnoreRules& ignore, bool dry_run,
                                      std::vector<std::string>* changed_files = nullptr);

// Copies the current home content of `entry` into the store ("backup").
SyncResult sync_entry_home_to_store(const std::filesystem::path& home_dir, const std::filesystem::path& store_dir,
                                     const ManagedEntry& entry, const IgnoreRules& ignore, bool dry_run, bool force);

// Applies the store content of `entry` onto its home location ("apply"),
// honoring copy/symlink mode.
SyncResult sync_entry_store_to_home(const std::filesystem::path& home_dir, const std::filesystem::path& store_dir,
                                     const ManagedEntry& entry, const IgnoreRules& ignore, bool dry_run, bool force);

struct SnapshotInfo {
    std::string id;             // e.g. "20260905T083000Z"
    std::filesystem::path path;  // absolute path to the snapshot directory
};

// Creates a full, timestamped snapshot of the current store content for all
// `entries` under <store>/.dotfile-manager/snapshots/<timestamp>/, mirroring
// each entry's store_path. Returns the created snapshot info, or std::nullopt
// with *error set on failure. When dry_run is true, no files are written and
// the returned SnapshotInfo.path reflects what WOULD have been created.
std::optional<SnapshotInfo> create_snapshot(const std::filesystem::path& store_dir,
                                             const std::vector<ManagedEntry>& entries, const IgnoreRules& ignore,
                                             bool dry_run, std::string* error);

// Lists existing snapshots, most recent first.
std::vector<SnapshotInfo> list_snapshots(const std::filesystem::path& store_dir);

// Restores a single entry's home content from a specific snapshot (applying
// the same destructive-replacement safety rules as sync_entry_store_to_home).
SyncResult restore_entry_from_snapshot(const std::filesystem::path& home_dir, const std::filesystem::path& store_dir,
                                        const std::string& snapshot_id, const ManagedEntry& entry,
                                        const IgnoreRules& ignore, bool dry_run, bool force);

}  // namespace dfm
