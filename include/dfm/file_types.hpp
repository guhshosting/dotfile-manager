// file_types.hpp - safe, non-throwing filesystem type inspection helpers.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace dfm {

enum class EntryKind {
    NotFound,       // nothing at this path (lstat failed with ENOENT-ish)
    Regular,
    Directory,
    Symlink,
    Fifo,
    Socket,
    BlockDevice,
    CharDevice,
    Unknown,        // stat succeeded but type is unrecognized
    Inaccessible,   // stat failed for a reason other than "not found" (e.g. permission)
};

std::string to_string(EntryKind kind);

// True for kinds that dotfile-manager is willing to copy/link/compare
// (Regular, Directory, Symlink). Special files are never touched.
bool is_supported_kind(EntryKind kind);

struct EntryInfo {
    EntryKind kind = EntryKind::NotFound;
    std::uintmax_t size = 0;              // regular files only
    std::filesystem::perms permissions{};  // mode bits, when available
    bool is_symlink = false;               // convenience flag (symlink_status based)
};

// Inspects `path` WITHOUT following a trailing symlink (uses lstat/
// symlink_status semantics), so a symlink is reported as EntryKind::Symlink
// rather than the type of its target.
EntryInfo inspect(const std::filesystem::path& path);

// Convenience helpers built on inspect().
bool exists_no_follow(const std::filesystem::path& path);
bool is_regular_no_follow(const std::filesystem::path& path);
bool is_directory_no_follow(const std::filesystem::path& path);
bool is_symlink_no_follow(const std::filesystem::path& path);

}  // namespace dfm
