// test_compare.cpp - unit tests for dfm/compare.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/compare.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

#include <set>

using namespace dfm;
using namespace dfm_test;

namespace {
IgnoreRules no_ignores() { return IgnoreRules(std::vector<std::string>{}); }
}  // namespace

// ---------------------------------------------------------------------------
// to_string helpers
// ---------------------------------------------------------------------------

TEST_CASE("to_string(EntryStatus) covers every enumerator distinctly") {
    std::vector<EntryStatus> all = {EntryStatus::InSync,        EntryStatus::Modified,   EntryStatus::MissingHome,
                                     EntryStatus::MissingStore,  EntryStatus::TypeMismatch,
                                     EntryStatus::SymlinkMismatch, EntryStatus::Conflict,   EntryStatus::Unknown};
    std::set<std::string> seen;
    for (auto s : all) seen.insert(to_string(s));
    CHECK(seen.size() == all.size());
}

TEST_CASE("to_string(DirChangeType) covers every enumerator distinctly") {
    std::vector<DirChangeType> all = {DirChangeType::Added, DirChangeType::Removed, DirChangeType::Modified,
                                       DirChangeType::TypeChanged, DirChangeType::SymlinkTargetChanged};
    std::set<std::string> seen;
    for (auto c : all) seen.insert(to_string(c));
    CHECK(seen.size() == all.size());
}

// ---------------------------------------------------------------------------
// list_tree
// ---------------------------------------------------------------------------

TEST_CASE("list_tree returns an empty map for a nonexistent root") {
    TempDir tmp;
    auto tree = list_tree(tmp.path() / "nope", no_ignores());
    CHECK(tree.empty());
}

TEST_CASE("list_tree lists regular files with content hashes") {
    TempDir tmp;
    write_file(tmp.path() / "a.txt", "hello");
    auto tree = list_tree(tmp.path(), no_ignores());
    REQUIRE(tree.count("a.txt") == 1);
    CHECK(tree.at("a.txt").kind == EntryKind::Regular);
    CHECK_FALSE(tree.at("a.txt").hash.empty());
}

TEST_CASE("list_tree recurses into nested directories") {
    TempDir tmp;
    write_file(tmp.path() / "sub" / "nested.txt", "x");
    auto tree = list_tree(tmp.path(), no_ignores());
    CHECK(tree.count("sub") == 1);
    CHECK(tree.at("sub").kind == EntryKind::Directory);
    CHECK(tree.count("sub/nested.txt") == 1);
}

TEST_CASE("list_tree records a symlinked directory as a symlink leaf and does not recurse into it") {
    TempDir tmp;
    fs::create_directory(tmp.path() / "realdir");
    write_file(tmp.path() / "realdir" / "inner.txt", "x");
    make_dir_symlink(tmp.path() / "realdir", tmp.path() / "linkdir");

    auto tree = list_tree(tmp.path(), no_ignores());
    REQUIRE(tree.count("linkdir") == 1);
    CHECK(tree.at("linkdir").kind == EntryKind::Symlink);
    CHECK(tree.count("linkdir/inner.txt") == 0);
}

TEST_CASE("list_tree honors ignore rules and skips ignored entries") {
    TempDir tmp;
    write_file(tmp.path() / "keep.txt", "x");
    write_file(tmp.path() / "skip.tmp", "x");
    IgnoreRules ignore({"*.tmp"});
    auto tree = list_tree(tmp.path(), ignore);
    CHECK(tree.count("keep.txt") == 1);
    CHECK(tree.count("skip.tmp") == 0);
}

TEST_CASE("list_tree records a symlink leaf's raw target") {
    TempDir tmp;
    write_file(tmp.path() / "target.txt", "x");
    make_symlink("target.txt", tmp.path() / "link.txt");
    auto tree = list_tree(tmp.path(), no_ignores());
    REQUIRE(tree.count("link.txt") == 1);
    CHECK(tree.at("link.txt").kind == EntryKind::Symlink);
    CHECK(tree.at("link.txt").symlink_target == fs::path("target.txt"));
}

// ---------------------------------------------------------------------------
// diff_trees
// ---------------------------------------------------------------------------

TEST_CASE("diff_trees reports no differences for identical trees") {
    TempDir tmp;
    write_file(tmp.path() / "a.txt", "same");
    auto tree = list_tree(tmp.path(), no_ignores());
    auto diffs = diff_trees(tree, tree);
    CHECK(diffs.empty());
}

TEST_CASE("diff_trees reports Added for a file present only in home") {
    std::map<std::string, TreeNode> store_tree;
    std::map<std::string, TreeNode> home_tree;
    home_tree["new.txt"] = TreeNode{EntryKind::Regular, "hash1", {}};
    auto diffs = diff_trees(store_tree, home_tree);
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].relative_path == "new.txt");
    CHECK(diffs[0].change == DirChangeType::Added);
}

TEST_CASE("diff_trees reports Removed for a file present only in store") {
    std::map<std::string, TreeNode> store_tree;
    std::map<std::string, TreeNode> home_tree;
    store_tree["gone.txt"] = TreeNode{EntryKind::Regular, "hash1", {}};
    auto diffs = diff_trees(store_tree, home_tree);
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].change == DirChangeType::Removed);
}

TEST_CASE("diff_trees reports Modified when regular file hashes differ") {
    std::map<std::string, TreeNode> store_tree, home_tree;
    store_tree["f.txt"] = TreeNode{EntryKind::Regular, "hashA", {}};
    home_tree["f.txt"] = TreeNode{EntryKind::Regular, "hashB", {}};
    auto diffs = diff_trees(store_tree, home_tree);
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].change == DirChangeType::Modified);
}

TEST_CASE("diff_trees reports TypeChanged when kind differs") {
    std::map<std::string, TreeNode> store_tree, home_tree;
    store_tree["f"] = TreeNode{EntryKind::Regular, "hashA", {}};
    home_tree["f"] = TreeNode{EntryKind::Directory, "", {}};
    auto diffs = diff_trees(store_tree, home_tree);
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].change == DirChangeType::TypeChanged);
}

TEST_CASE("diff_trees reports SymlinkTargetChanged when symlink targets differ") {
    std::map<std::string, TreeNode> store_tree, home_tree;
    store_tree["l"] = TreeNode{EntryKind::Symlink, "", fs::path("old-target")};
    home_tree["l"] = TreeNode{EntryKind::Symlink, "", fs::path("new-target")};
    auto diffs = diff_trees(store_tree, home_tree);
    REQUIRE(diffs.size() == 1);
    CHECK(diffs[0].change == DirChangeType::SymlinkTargetChanged);
}

TEST_CASE("diff_trees reports no diff when symlink targets match") {
    std::map<std::string, TreeNode> store_tree, home_tree;
    store_tree["l"] = TreeNode{EntryKind::Symlink, "", fs::path("same-target")};
    home_tree["l"] = TreeNode{EntryKind::Symlink, "", fs::path("same-target")};
    CHECK(diff_trees(store_tree, home_tree).empty());
}

// ---------------------------------------------------------------------------
// compute_status
// ---------------------------------------------------------------------------

TEST_CASE("compute_status reports MissingStore when the store copy is absent") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.h(".bashrc"), "content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::MissingStore);
}

TEST_CASE("compute_status reports MissingHome when the home file is absent") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.s("bash/.bashrc"), "content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::MissingHome);
}

TEST_CASE("compute_status reports InSync for identical copy-mode file content") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.h(".bashrc"), "same content");
    write_file(fh.s("bash/.bashrc"), "same content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::InSync);
}

TEST_CASE("compute_status reports Modified for differing copy-mode file content") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.h(".bashrc"), "home version");
    write_file(fh.s("bash/.bashrc"), "store version");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::Modified);
}

TEST_CASE("compute_status reports TypeMismatch when store is a file but home is a directory") {
    FakeHome fh;
    fs::create_directories(fh.store);
    fs::create_directory(fh.h(".bashrc"));
    write_file(fh.s("bash/.bashrc"), "content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::TypeMismatch);
}

TEST_CASE("compute_status reports SymlinkMismatch for copy-mode entry whose home is a symlink") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.s("bash/.bashrc"), "content");
    write_file(fh.h("real.txt"), "content");
    make_symlink(fh.h("real.txt"), fh.h(".bashrc"));
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::SymlinkMismatch);
}

TEST_CASE("compute_status reports SymlinkMismatch for symlink-mode entry whose home is real content") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.s("bash/.bashrc"), "content");
    write_file(fh.h(".bashrc"), "content");  // real file, not a symlink
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Symlink, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::SymlinkMismatch);
}

TEST_CASE("compute_status reports InSync for symlink-mode entry correctly linked to the store") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.s("bash/.bashrc"), "content");
    make_symlink(fh.s("bash/.bashrc"), fh.h(".bashrc"));
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Symlink, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::InSync);
}

TEST_CASE("compute_status reports SymlinkMismatch for symlink-mode entry pointing elsewhere") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.s("bash/.bashrc"), "content");
    write_file(fh.h("other.txt"), "content");
    make_symlink(fh.h("other.txt"), fh.h(".bashrc"));
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Symlink, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::SymlinkMismatch);
}

TEST_CASE("compute_status reports Conflict for a path-safety violation") {
    FakeHome fh;
    fs::create_directories(fh.store);
    ManagedEntry entry{"../../etc/passwd", "x", ManageMode::Copy, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::Conflict);
}

TEST_CASE("compute_status reports InSync for identical directory entries") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.h("proj/a.txt"), "x");
    write_file(fh.s("proj-store/a.txt"), "x");
    ManagedEntry entry{"proj", "proj-store", ManageMode::Copy, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::InSync);
    CHECK(result.is_directory_entry);
}

TEST_CASE("compute_status reports Modified for a directory with differing content and populates directory_changes") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.h("proj/a.txt"), "home-version");
    write_file(fh.s("proj-store/a.txt"), "store-version");
    ManagedEntry entry{"proj", "proj-store", ManageMode::Copy, {}, {}};
    auto result = compute_status(fh.home, fh.store, entry, no_ignores());
    CHECK(result.status == EntryStatus::Modified);
    CHECK(result.is_directory_entry);
    CHECK_FALSE(result.directory_changes.empty());
}
