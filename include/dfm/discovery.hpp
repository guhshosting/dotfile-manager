// discovery.hpp - conservative, read-only dotfile discovery.
//
// Discovery never scans the whole home directory. It looks at:
//   - direct (depth 1) dotfile entries under $HOME
//   - direct (depth 1) entries under $HOME/.config
// and classifies each candidate as ordinary or sensitive. Sensitive
// candidates are still reported (so the user knows they exist) but are
// flagged so that `add` requires an explicit opt-in flag, and their
// contents are never read or printed during discovery.
//
// SPDX-License-Identifier: MIT
#pragma once

#include "dfm/file_types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dfm {

struct DiscoveredEntry {
    std::string relative_path;  // relative to HOME, POSIX-style, e.g. ".config/htop"
    EntryKind kind = EntryKind::NotFound;
    bool sensitive = false;
    std::string note;  // human-readable classification / reason
};

// Returns true if `relative_path` (relative to HOME, no leading "./") is
// considered sensitive by dotfile-manager's built-in heuristics (SSH keys,
// GnuPG data, cloud credentials, password stores, etc).
bool is_sensitive_path(const std::string& relative_path);

// Performs bounded, read-only discovery under `home_dir` (which must be an
// existing, canonical directory). Never recurses more than two levels deep
// (HOME -> .config -> child) and never scans excluded locations such as
// ~/.cache, ~/.local/share, browser profiles, Trash, or Downloads.
std::vector<DiscoveredEntry> discover(const std::filesystem::path& home_dir);

}  // namespace dfm
