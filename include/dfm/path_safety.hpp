// path_safety.hpp - reusable, defensive path handling.
//
// This module is the single place where dotfile-manager reasons about
// whether a path is safe to touch. Every command that reads or writes
// filesystem content MUST route destination/source resolution through
// resolve_within() (or validate_relative_path() for config-stored strings)
// rather than concatenating paths manually.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace dfm {

enum class PathSafetyError {
    None = 0,
    Empty,
    Absolute,
    ParentTraversal,
    NullByte,
    EmptyComponent,
    EscapesBase,
    SymlinkEscapesBase,
    FilesystemLoop,
    ParentNotDirectory,
    BrokenIntermediateSymlink,
    CanonicalizeFailed,
    IsRoot,
};

// Human readable, stable description for an error code (does not include the
// offending path; callers should include that separately in messages).
std::string to_string(PathSafetyError err);

struct PathSafetyResult {
    PathSafetyError error = PathSafetyError::None;
    std::string message;
    std::filesystem::path resolved;  // valid (lexically-final) path when ok()

    [[nodiscard]] bool ok() const { return error == PathSafetyError::None; }
    explicit operator bool() const { return ok(); }
};

// Validates a relative path string as stored in configuration (home_path /
// store_path fields). Rejects:
//   - empty strings
//   - absolute paths
//   - any ".." path component (no traversal, ever)
//   - embedded NUL bytes
//   - empty path components (e.g. results of malformed separators)
// Does NOT touch the filesystem. `resolved` on success is the lexically
// normalized relative path (as a relative std::filesystem::path).
PathSafetyResult validate_relative_path(const std::string& rel);

// Canonicalizes `p`, catching filesystem_error (permission denied, ELOOP,
// dangling target, etc.) and returning std::nullopt instead of throwing.
std::optional<std::filesystem::path> try_canonical(const std::filesystem::path& p);

// Like try_canonical but works on paths whose final component may not exist
// (std::filesystem::weakly_canonical semantics), still guarding against
// exceptions from symlink loops in existing prefix components.
std::optional<std::filesystem::path> try_weakly_canonical(const std::filesystem::path& p);

// Returns true if `candidate` is lexically equal to or a descendant of
// `base`. Both arguments MUST already be canonical/absolute and free of ".."
// components; this function performs no filesystem access.
bool is_within(const std::filesystem::path& base, const std::filesystem::path& candidate);

// Resolves `relative` against `base_dir`, which must already be an existing,
// canonical, absolute directory (see try_canonical). Guarantees:
//   - `relative` passes validate_relative_path()
//   - every existing intermediate directory component, after resolving any
//     symlinks, still lies within `base_dir`
//   - symlink cycles among intermediate components are detected and rejected
// The final path component (the "leaf") is appended lexically and is NOT
// itself dereferenced/canonicalized, so callers can still detect that the
// leaf itself is a symlink and decide how to handle it explicitly.
PathSafetyResult resolve_within(const std::filesystem::path& base_dir, const std::string& relative);

// Reads the immediate (single-level, non-recursive) target of a symlink.
// Returns std::nullopt if `link_path` does not exist or is not a symlink, or
// if the link cannot be read.
std::optional<std::filesystem::path> read_symlink_target(const std::filesystem::path& link_path);

// Resolves a symlink target string relative to the directory containing the
// link (per POSIX symlink semantics) without further following the result.
std::filesystem::path resolve_symlink_target(const std::filesystem::path& link_path,
                                              const std::filesystem::path& raw_target);

// Ensures `dir` exists (creating intermediate directories as needed when
// `create_if_missing` is true) and returns its canonical form. This performs
// filesystem writes only for directory creation, never for arbitrary files,
// and only ever creates directories that are descendants of the first
// existing ancestor of `dir` (i.e. it cannot "jump" elsewhere via a symlink
// substituted mid-path after the check, beyond the same TOCTOU caveat that
// applies to all POSIX path operations - see docs/ARCHITECTURE.md).
std::optional<std::filesystem::path> ensure_directory_canonical(const std::filesystem::path& dir,
                                                                  bool create_if_missing,
                                                                  std::string* error_message);

// Returns true if `p` is the filesystem root ("/") once canonicalized. Used
// to refuse operations against a base directory of "/".
bool is_filesystem_root(const std::filesystem::path& p);

}  // namespace dfm
