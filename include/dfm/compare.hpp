// compare.hpp - the status/comparison engine.
// SPDX-License-Identifier: MIT
#pragma once

#include "dfm/config.hpp"
#include "dfm/file_types.hpp"
#include "dfm/ignore_rules.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace dfm {

enum class EntryStatus {
    InSync,
    Modified,
    MissingHome,
    MissingStore,
    TypeMismatch,
    SymlinkMismatch,
    Conflict,
    Unknown,
};

std::string to_string(EntryStatus status);

enum class DirChangeType { Added, Removed, Modified, TypeChanged, SymlinkTargetChanged };

std::string to_string(DirChangeType change);

struct DirDiffItem {
    std::string relative_path;  // relative to the entry root, '/' separated
    DirChangeType change;
};

struct TreeNode {
    EntryKind kind = EntryKind::NotFound;
    std::string hash;             // regular files only (sha256 hex)
    std::filesystem::path symlink_target;  // symlinks only, raw (unresolved) target
};

// Recursively lists a directory tree rooted at `root` (which must exist and
// be a directory), keyed by POSIX-style relative path. Symlinked
// directories are recorded as symlink leaves and are NOT recursed into
// (this bounds traversal and avoids following symlink loops). Paths
// matching `ignore` are skipped entirely (and, for ignored directories, not
// recursed into). Special files (fifo/socket/device) are recorded with
// their EntryKind but never read.
std::map<std::string, TreeNode> list_tree(const std::filesystem::path& root, const IgnoreRules& ignore);

// Compares two previously-listed trees and returns the set of differences.
std::vector<DirDiffItem> diff_trees(const std::map<std::string, TreeNode>& store_tree,
                                     const std::map<std::string, TreeNode>& home_tree);

struct EntryStatusResult {
    EntryStatus status = EntryStatus::Unknown;
    std::string detail;
    std::filesystem::path home_abs;   // best-effort resolved path (may be partial on error)
    std::filesystem::path store_abs;
    bool is_directory_entry = false;
    std::vector<DirDiffItem> directory_changes;  // populated when is_directory_entry && Modified
};

// Computes the status of a single managed entry by comparing its store copy
// against its home location. `home_dir` and `store_dir` MUST already be
// canonical, existing, absolute directories.
EntryStatusResult compute_status(const std::filesystem::path& home_dir, const std::filesystem::path& store_dir,
                                  const ManagedEntry& entry, const IgnoreRules& ignore);

}  // namespace dfm
