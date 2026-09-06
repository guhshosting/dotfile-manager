#include "dfm/discovery.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>

namespace dfm {

namespace fs = std::filesystem;

namespace {

// Top-level (depth 1, directly under $HOME) dotfile/dir names that are
// never surfaced by discovery at all: large-data or cache-like locations,
// browser/mail profiles, trash, downloads. Users may still `add` paths
// under these manually - discovery simply does not suggest them.
const std::set<std::string>& excluded_top_level() {
    static const std::set<std::string> s = {
        "cache", "local", "Trash", "trash", "Downloads", "downloads", "mozilla",
        "thunderbird", "steam", "Steam", "wine", ".Trash",
    };
    return s;
}

// Top-level names that ARE surfaced but flagged sensitive (opt-in required
// before `add` will accept them).
const std::set<std::string>& sensitive_top_level() {
    static const std::set<std::string> s = {
        "ssh", "gnupg", "gpg", "aws", "password-store", "docker",
        "netrc", "git-credentials", "kube", "config/gcloud",
    };
    return s;
}

// Children of $HOME/.config that are never surfaced: browser/chat-app
// profile directories that hold large amounts of cache/session data rather
// than portable configuration.
const std::set<std::string>& excluded_config_children() {
    static const std::set<std::string> s = {
        "google-chrome", "chromium",  "BraveSoftware", "microsoft-edge", "vivaldi",
        "Slack",         "discord",   "Element",       "Signal",         "syncthing",
        "spotify",       "Code",      "Code - Insiders",
    };
    return s;
}

// Children of $HOME/.config surfaced but flagged sensitive.
const std::set<std::string>& sensitive_config_children() {
    static const std::set<std::string> s = {"gcloud", "gh", "rclone", "op", "doppler"};
    return s;
}

bool starts_with_dot(const std::string& name) { return !name.empty() && name.front() == '.'; }

}  // namespace

bool is_sensitive_path(const std::string& relative_path) {
    fs::path p(relative_path);
    auto it = p.begin();
    if (it == p.end()) return false;
    std::string first = it->string();
    if (starts_with_dot(first)) first = first.substr(1);

    if (sensitive_top_level().count(first) > 0) return true;

    if (first == "config") {
        auto next = std::next(it);
        if (next != p.end()) {
            std::string second = next->string();
            if (sensitive_config_children().count(second) > 0) return true;
        }
    }

    // Generic name-based heuristics for anything explicitly checked (used
    // by `add` even outside of discovery output), catching common secret
    // filenames regardless of location.
    std::string lower;
    lower.reserve(relative_path.size());
    for (char c : relative_path) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    static const std::array<const char*, 8> needles = {
        "credential", "token", ".netrc", "id_rsa", "id_ed25519", "known_hosts", ".npmrc", "cookies.sqlite"};
    for (const char* needle : needles) {
        if (lower.find(needle) != std::string::npos) return true;
    }

    return false;
}

std::vector<DiscoveredEntry> discover(const fs::path& home_dir) {
    std::vector<DiscoveredEntry> results;
    if (!is_directory_no_follow(home_dir)) return results;

    std::error_code ec;
    for (const auto& child : fs::directory_iterator(home_dir, fs::directory_options::skip_permission_denied, ec)) {
        std::string name = child.path().filename().string();
        if (!starts_with_dot(name)) continue;
        if (name == "." || name == "..") continue;

        std::string bare = name.substr(1);
        if (excluded_top_level().count(bare) > 0) continue;

        auto info = inspect(child.path());
        if (!is_supported_kind(info.kind)) continue;  // skip special files silently

        if (bare == "config") {
            // Recurse exactly one more level into .config, never deeper.
            for (const auto& cfg_child :
                 fs::directory_iterator(child.path(), fs::directory_options::skip_permission_denied, ec)) {
                std::string cfg_name = cfg_child.path().filename().string();
                if (excluded_config_children().count(cfg_name) > 0) continue;
                auto cfg_info = inspect(cfg_child.path());
                if (!is_supported_kind(cfg_info.kind)) continue;

                std::string rel = ".config/" + cfg_name;
                DiscoveredEntry entry;
                entry.relative_path = rel;
                entry.kind = cfg_info.kind;
                entry.sensitive = is_sensitive_path(rel);
                entry.note = entry.sensitive ? "sensitive: requires explicit opt-in to add" : "config entry";
                results.push_back(std::move(entry));
            }
            continue;
        }

        DiscoveredEntry entry;
        entry.relative_path = name;
        entry.kind = info.kind;
        entry.sensitive = is_sensitive_path(name);
        entry.note = entry.sensitive ? "sensitive: requires explicit opt-in to add"
                                      : (info.kind == EntryKind::Directory ? "top-level directory" : "top-level dotfile");
        results.push_back(std::move(entry));
    }

    std::sort(results.begin(), results.end(),
              [](const DiscoveredEntry& a, const DiscoveredEntry& b) { return a.relative_path < b.relative_path; });
    return results;
}

}  // namespace dfm
