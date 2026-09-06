// test_discovery.cpp - unit tests for dfm/discovery.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/discovery.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

#include <algorithm>

using namespace dfm;
using namespace dfm_test;

namespace {
bool contains_path(const std::vector<DiscoveredEntry>& entries, const std::string& rel) {
    return std::any_of(entries.begin(), entries.end(), [&](const DiscoveredEntry& e) { return e.relative_path == rel; });
}
const DiscoveredEntry* find_path(const std::vector<DiscoveredEntry>& entries, const std::string& rel) {
    for (auto& e : entries)
        if (e.relative_path == rel) return &e;
    return nullptr;
}
}  // namespace

// ---------------------------------------------------------------------------
// is_sensitive_path
// ---------------------------------------------------------------------------

TEST_CASE("is_sensitive_path flags .ssh") { CHECK(is_sensitive_path(".ssh")); }
TEST_CASE("is_sensitive_path flags .gnupg") { CHECK(is_sensitive_path(".gnupg")); }
TEST_CASE("is_sensitive_path flags .aws") { CHECK(is_sensitive_path(".aws")); }
TEST_CASE("is_sensitive_path flags .password-store") { CHECK(is_sensitive_path(".password-store")); }
TEST_CASE("is_sensitive_path flags .config/gcloud") { CHECK(is_sensitive_path(".config/gcloud")); }
TEST_CASE("is_sensitive_path flags .config/gh") { CHECK(is_sensitive_path(".config/gh")); }

TEST_CASE("is_sensitive_path does not flag ordinary .config children") {
    CHECK_FALSE(is_sensitive_path(".config/htop"));
    CHECK_FALSE(is_sensitive_path(".config/nvim"));
}

TEST_CASE("is_sensitive_path does not flag common dotfiles") {
    CHECK_FALSE(is_sensitive_path(".bashrc"));
    CHECK_FALSE(is_sensitive_path(".vimrc"));
    CHECK_FALSE(is_sensitive_path(".gitconfig"));
    CHECK_FALSE(is_sensitive_path(".profile"));
}

TEST_CASE("is_sensitive_path flags filenames containing 'credential'") {
    CHECK(is_sensitive_path(".git-credentials"));
}

TEST_CASE("is_sensitive_path flags SSH key-like filenames anywhere") {
    CHECK(is_sensitive_path("some/path/id_rsa"));
    CHECK(is_sensitive_path("some/path/id_ed25519"));
}

TEST_CASE("is_sensitive_path flags .npmrc (may contain auth tokens)") { CHECK(is_sensitive_path(".npmrc")); }

TEST_CASE("is_sensitive_path flags browser cookie databases") { CHECK(is_sensitive_path("profile/cookies.sqlite")); }

TEST_CASE("is_sensitive_path returns false for an empty path") { CHECK_FALSE(is_sensitive_path("")); }

TEST_CASE("is_sensitive_path is case-insensitive for name-based heuristics") {
    CHECK(is_sensitive_path("MY_TOKEN.txt"));
    CHECK(is_sensitive_path("Credential-Store.db"));
}

// ---------------------------------------------------------------------------
// discover
// ---------------------------------------------------------------------------

TEST_CASE("discover returns empty for a nonexistent home directory") {
    TempDir tmp;
    auto results = discover(tmp.path() / "nope");
    CHECK(results.empty());
}

TEST_CASE("discover surfaces top-level dotfiles") {
    TempDir tmp;
    write_file(tmp.path() / ".bashrc", "x");
    write_file(tmp.path() / ".vimrc", "x");
    auto results = discover(tmp.path());
    CHECK(contains_path(results, ".bashrc"));
    CHECK(contains_path(results, ".vimrc"));
}

TEST_CASE("discover ignores non-dotfile entries at the top level") {
    TempDir tmp;
    write_file(tmp.path() / "regular.txt", "x");
    auto results = discover(tmp.path());
    CHECK_FALSE(contains_path(results, "regular.txt"));
}

TEST_CASE("discover never surfaces .cache") {
    TempDir tmp;
    write_file(tmp.path() / ".cache" / "somefile", "x");
    auto results = discover(tmp.path());
    CHECK_FALSE(contains_path(results, ".cache"));
}

TEST_CASE("discover never surfaces .local (covers .local/share)") {
    TempDir tmp;
    write_file(tmp.path() / ".local" / "share" / "app" / "data.db", "x");
    auto results = discover(tmp.path());
    CHECK_FALSE(contains_path(results, ".local"));
}

TEST_CASE("discover never surfaces Trash or Downloads") {
    TempDir tmp;
    fs::create_directories(tmp.path() / ".Trash");
    write_file(tmp.path() / ".Downloads" / "file.zip", "x");
    auto results = discover(tmp.path());
    for (auto& e : results) {
        CHECK(e.relative_path.find("Trash") == std::string::npos);
        CHECK(e.relative_path.find("Downloads") == std::string::npos);
    }
}

TEST_CASE("discover does not surface the .config directory itself, only its children") {
    TempDir tmp;
    write_file(tmp.path() / ".config" / "htop" / "htoprc", "x");
    auto results = discover(tmp.path());
    CHECK_FALSE(contains_path(results, ".config"));
    CHECK(contains_path(results, ".config/htop"));
}

TEST_CASE("discover recurses exactly one level into .config, not deeper") {
    TempDir tmp;
    write_file(tmp.path() / ".config" / "nvim" / "init.lua", "x");
    auto results = discover(tmp.path());
    CHECK(contains_path(results, ".config/nvim"));
    CHECK_FALSE(contains_path(results, ".config/nvim/init.lua"));
}

TEST_CASE("discover excludes known browser/chat profile directories under .config") {
    TempDir tmp;
    fs::create_directories(tmp.path() / ".config" / "google-chrome");
    fs::create_directories(tmp.path() / ".config" / "discord");
    auto results = discover(tmp.path());
    CHECK_FALSE(contains_path(results, ".config/google-chrome"));
    CHECK_FALSE(contains_path(results, ".config/discord"));
}

TEST_CASE("discover flags .ssh as sensitive with an explanatory note") {
    TempDir tmp;
    fs::create_directories(tmp.path() / ".ssh");
    auto results = discover(tmp.path());
    auto* entry = find_path(results, ".ssh");
    REQUIRE(entry != nullptr);
    CHECK(entry->sensitive);
    CHECK_FALSE(entry->note.empty());
}

TEST_CASE("discover flags .config/gcloud as sensitive") {
    TempDir tmp;
    fs::create_directories(tmp.path() / ".config" / "gcloud");
    auto results = discover(tmp.path());
    auto* entry = find_path(results, ".config/gcloud");
    REQUIRE(entry != nullptr);
    CHECK(entry->sensitive);
}

TEST_CASE("discover marks ordinary entries as not sensitive") {
    TempDir tmp;
    write_file(tmp.path() / ".bashrc", "x");
    auto results = discover(tmp.path());
    auto* entry = find_path(results, ".bashrc");
    REQUIRE(entry != nullptr);
    CHECK_FALSE(entry->sensitive);
}

TEST_CASE("discover records the correct EntryKind for files and directories") {
    TempDir tmp;
    write_file(tmp.path() / ".bashrc", "x");
    fs::create_directories(tmp.path() / ".config" / "htop");
    auto results = discover(tmp.path());
    auto* file_entry = find_path(results, ".bashrc");
    auto* dir_entry = find_path(results, ".config/htop");
    REQUIRE(file_entry != nullptr);
    REQUIRE(dir_entry != nullptr);
    CHECK(file_entry->kind == EntryKind::Regular);
    CHECK(dir_entry->kind == EntryKind::Directory);
}

TEST_CASE("discover results are sorted by relative_path") {
    TempDir tmp;
    write_file(tmp.path() / ".zshrc", "x");
    write_file(tmp.path() / ".bashrc", "x");
    write_file(tmp.path() / ".aliases", "x");
    auto results = discover(tmp.path());
    CHECK(std::is_sorted(results.begin(), results.end(),
                          [](const DiscoveredEntry& a, const DiscoveredEntry& b) {
                              return a.relative_path < b.relative_path;
                          }));
}

TEST_CASE("discover never reports contents; note text never includes file content") {
    TempDir tmp;
    write_file(tmp.path() / ".bashrc", "SECRET_MARKER_XYZ");
    auto results = discover(tmp.path());
    for (auto& e : results) CHECK(e.note.find("SECRET_MARKER_XYZ") == std::string::npos);
}
