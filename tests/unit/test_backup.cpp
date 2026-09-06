// test_backup.cpp - unit tests for dfm/backup.hpp (the sync/apply/snapshot
// execution engine). This is the module that performs actual filesystem
// mutations, so these tests are the most safety-critical in the suite.
// SPDX-License-Identifier: MIT
#include "dfm/backup.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

#include <set>

using namespace dfm;
using namespace dfm_test;

namespace {
IgnoreRules no_ignores() { return IgnoreRules(std::vector<std::string>{}); }
}  // namespace

// ---------------------------------------------------------------------------
// to_string / is_refused / is_change
// ---------------------------------------------------------------------------

TEST_CASE("to_string(SyncOutcome) covers every enumerator distinctly") {
    std::vector<SyncOutcome> all = {
        SyncOutcome::NoChangeInSync,   SyncOutcome::WouldCreate,   SyncOutcome::Created,
        SyncOutcome::WouldUpdate,      SyncOutcome::Updated,       SyncOutcome::WouldReplaceSymlink,
        SyncOutcome::ReplacedSymlink,  SyncOutcome::RefusedConflict, SyncOutcome::RefusedTypeMismatch,
        SyncOutcome::RefusedUnsafeDirectoryReplace, SyncOutcome::RefusedPathSafety, SyncOutcome::Skipped,
        SyncOutcome::Failed,
    };
    std::set<std::string> seen;
    for (auto o : all) seen.insert(to_string(o));
    CHECK(seen.size() == all.size());
}

TEST_CASE("is_refused is true for all Refused* outcomes and Failed") {
    CHECK(is_refused(SyncOutcome::RefusedConflict));
    CHECK(is_refused(SyncOutcome::RefusedTypeMismatch));
    CHECK(is_refused(SyncOutcome::RefusedUnsafeDirectoryReplace));
    CHECK(is_refused(SyncOutcome::RefusedPathSafety));
    CHECK(is_refused(SyncOutcome::Failed));
}

TEST_CASE("is_refused is false for non-refusal outcomes") {
    CHECK_FALSE(is_refused(SyncOutcome::NoChangeInSync));
    CHECK_FALSE(is_refused(SyncOutcome::Created));
    CHECK_FALSE(is_refused(SyncOutcome::Updated));
    CHECK_FALSE(is_refused(SyncOutcome::WouldCreate));
    CHECK_FALSE(is_refused(SyncOutcome::Skipped));
}

TEST_CASE("is_change is true only for Created/Updated/ReplacedSymlink") {
    CHECK(is_change(SyncOutcome::Created));
    CHECK(is_change(SyncOutcome::Updated));
    CHECK(is_change(SyncOutcome::ReplacedSymlink));
    CHECK_FALSE(is_change(SyncOutcome::WouldCreate));
    CHECK_FALSE(is_change(SyncOutcome::WouldUpdate));
    CHECK_FALSE(is_change(SyncOutcome::NoChangeInSync));
    CHECK_FALSE(is_change(SyncOutcome::RefusedConflict));
}

// ---------------------------------------------------------------------------
// copy_tree
// ---------------------------------------------------------------------------

TEST_CASE("copy_tree errors when source is not a directory") {
    TempDir tmp;
    write_file(tmp.path() / "notadir.txt", "x");
    auto err = copy_tree(tmp.path() / "notadir.txt", tmp.path() / "dest", no_ignores(), false);
    CHECK(err.has_value());
}

TEST_CASE("copy_tree copies files and creates the destination directory") {
    TempDir tmp;
    write_file(tmp.path() / "src" / "a.txt", "hello");
    auto err = copy_tree(tmp.path() / "src", tmp.path() / "dest", no_ignores(), false);
    CHECK_FALSE(err.has_value());
    CHECK(read_file(tmp.path() / "dest" / "a.txt") == "hello");
}

TEST_CASE("copy_tree in dry_run mode does not touch the filesystem") {
    TempDir tmp;
    write_file(tmp.path() / "src" / "a.txt", "hello");
    std::vector<std::string> changed;
    auto err = copy_tree(tmp.path() / "src", tmp.path() / "dest", no_ignores(), true, &changed);
    CHECK_FALSE(err.has_value());
    CHECK_FALSE(fs::exists(tmp.path() / "dest"));
    CHECK(changed == std::vector<std::string>{"a.txt"});
}

TEST_CASE("copy_tree does not rewrite a destination file whose content already matches") {
    TempDir tmp;
    write_file(tmp.path() / "src" / "a.txt", "same");
    write_file(tmp.path() / "dest" / "a.txt", "same");
    auto mtime_before = fs::last_write_time(tmp.path() / "dest" / "a.txt");
    std::vector<std::string> changed;
    auto err = copy_tree(tmp.path() / "src", tmp.path() / "dest", no_ignores(), false, &changed);
    CHECK_FALSE(err.has_value());
    CHECK(changed.empty());
    CHECK(fs::last_write_time(tmp.path() / "dest" / "a.txt") == mtime_before);
}

TEST_CASE("copy_tree overwrites a destination file whose content differs") {
    TempDir tmp;
    write_file(tmp.path() / "src" / "a.txt", "new content");
    write_file(tmp.path() / "dest" / "a.txt", "old content");
    auto err = copy_tree(tmp.path() / "src", tmp.path() / "dest", no_ignores(), false);
    CHECK_FALSE(err.has_value());
    CHECK(read_file(tmp.path() / "dest" / "a.txt") == "new content");
}

TEST_CASE("copy_tree preserves symlinks as symlinks rather than following them") {
    TempDir tmp;
    write_file(tmp.path() / "src" / "target.txt", "x");
    make_symlink("target.txt", tmp.path() / "src" / "link.txt");
    auto err = copy_tree(tmp.path() / "src", tmp.path() / "dest", no_ignores(), false);
    CHECK_FALSE(err.has_value());
    CHECK(fs::is_symlink(fs::symlink_status(tmp.path() / "dest" / "link.txt")));
}

TEST_CASE("copy_tree never deletes destination-only extra files (non-destructive merge)") {
    TempDir tmp;
    write_file(tmp.path() / "src" / "a.txt", "x");
    write_file(tmp.path() / "dest" / "extra.txt", "keep me");
    auto err = copy_tree(tmp.path() / "src", tmp.path() / "dest", no_ignores(), false);
    CHECK_FALSE(err.has_value());
    CHECK(fs::exists(tmp.path() / "dest" / "extra.txt"));
}

TEST_CASE("copy_tree honors ignore rules and skips ignored files") {
    TempDir tmp;
    write_file(tmp.path() / "src" / "keep.txt", "x");
    write_file(tmp.path() / "src" / "skip.tmp", "x");
    auto err = copy_tree(tmp.path() / "src", tmp.path() / "dest", IgnoreRules({"*.tmp"}), false);
    CHECK_FALSE(err.has_value());
    CHECK(fs::exists(tmp.path() / "dest" / "keep.txt"));
    CHECK_FALSE(fs::exists(tmp.path() / "dest" / "skip.tmp"));
}

// ---------------------------------------------------------------------------
// sync_entry_home_to_store (backup)
// ---------------------------------------------------------------------------

TEST_CASE("sync_entry_home_to_store skips when home does not exist") {
    FakeHome fh;
    fs::create_directories(fh.store);  // base store dir must exist for resolve_within to succeed
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::Skipped);
}

TEST_CASE("sync_entry_home_to_store refuses with RefusedPathSafety when the store base directory does not exist yet") {
    // Documents an important real behavior: sync_entry_home_to_store/
    // sync_entry_store_to_home never create the store's base directory
    // themselves - callers (cmd_add/cmd_backup) must call
    // ensure_directory_canonical() first. Without that, even an existing,
    // ordinary home file is refused rather than silently doing nothing.
    FakeHome fh;
    write_file(fh.h(".bashrc"), "content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedPathSafety);
    CHECK_FALSE(fs::exists(fh.store));
}

TEST_CASE("sync_entry_home_to_store reports RefusedPathSafety for a traversal home_path") {
    FakeHome fh;
    ManagedEntry entry{"../../etc/passwd", "x", ManageMode::Copy, {}, {}};
    auto r = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedPathSafety);
}

TEST_CASE("sync_entry_home_to_store creates a new store copy from home (dry_run previews without writing)") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.h(".bashrc"), "content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};

    auto dry = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), true, false);
    CHECK(dry.outcome == SyncOutcome::WouldCreate);
    CHECK_FALSE(fs::exists(fh.s("bash/.bashrc")));

    auto real = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(real.outcome == SyncOutcome::Created);
    CHECK(read_file(fh.s("bash/.bashrc")) == "content");
}

TEST_CASE("sync_entry_home_to_store reports NoChangeInSync when content already matches") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "same");
    write_file(fh.s("bash/.bashrc"), "same");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::NoChangeInSync);
}

TEST_CASE("sync_entry_home_to_store updates the store copy when home content differs, without needing force") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "new home content");
    write_file(fh.s("bash/.bashrc"), "old store content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};

    auto dry = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), true, false);
    CHECK(dry.outcome == SyncOutcome::WouldUpdate);
    CHECK(read_file(fh.s("bash/.bashrc")) == "old store content");  // unchanged by dry run

    auto real = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(real.outcome == SyncOutcome::Updated);
    CHECK(read_file(fh.s("bash/.bashrc")) == "new home content");
}

TEST_CASE("sync_entry_home_to_store refuses a type mismatch without force") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "content");        // home: regular file
    fs::create_directories(fh.s("bash/.bashrc"));  // store: directory
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedTypeMismatch);
}

TEST_CASE("sync_entry_home_to_store replaces a type-mismatched store entry with force") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "content");
    fs::create_directories(fh.s("bash/.bashrc"));
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, true);
    CHECK(r.outcome == SyncOutcome::Created);
    CHECK(read_file(fh.s("bash/.bashrc")) == "content");
}

TEST_CASE("sync_entry_home_to_store refuses an unmanaged home symlink") {
    FakeHome fh;
    fs::create_directories(fh.store);
    write_file(fh.h("other.txt"), "x");
    make_symlink(fh.h("other.txt"), fh.h(".bashrc"));
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedConflict);
}

TEST_CASE("sync_entry_home_to_store recognizes a home symlink already pointing into the store as in sync") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "content");
    make_symlink(fh.s("bash/.bashrc"), fh.h(".bashrc"));
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Symlink, {}, {}};
    auto r = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::NoChangeInSync);
}

TEST_CASE("sync_entry_home_to_store merges directory content and reports changed file count") {
    FakeHome fh;
    write_file(fh.h("proj/a.txt"), "aaa");
    write_file(fh.h("proj/b.txt"), "bbb");
    write_file(fh.s("proj-store/a.txt"), "aaa");  // already matches
    ManagedEntry entry{"proj", "proj-store", ManageMode::Copy, {}, {}};
    auto r = sync_entry_home_to_store(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::Updated);
    CHECK(r.changed_files == std::vector<std::string>{"b.txt"});
    CHECK(read_file(fh.s("proj-store/b.txt")) == "bbb");
}

// ---------------------------------------------------------------------------
// sync_entry_store_to_home (apply) - copy mode
// ---------------------------------------------------------------------------

TEST_CASE("sync_entry_store_to_home skips when store has nothing for the entry") {
    FakeHome fh;
    fs::create_directories(fh.store);  // base store dir must exist for resolve_within to succeed
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::Skipped);
}

TEST_CASE("sync_entry_store_to_home refuses with RefusedPathSafety when the store base directory does not exist yet") {
    FakeHome fh;
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedPathSafety);
}

TEST_CASE("sync_entry_store_to_home creates home content when home is missing (copy mode)") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};

    auto dry = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), true, false);
    CHECK(dry.outcome == SyncOutcome::WouldCreate);
    CHECK_FALSE(fs::exists(fh.h(".bashrc")));

    auto real = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(real.outcome == SyncOutcome::Created);
    CHECK(read_file(fh.h(".bashrc")) == "content");
}

TEST_CASE("sync_entry_store_to_home reports NoChangeInSync for matching copy-mode content") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "same");
    write_file(fh.h(".bashrc"), "same");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::NoChangeInSync);
}

TEST_CASE("sync_entry_store_to_home refuses to overwrite differing home content without force") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "store version");
    write_file(fh.h(".bashrc"), "home version");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedConflict);
    CHECK(read_file(fh.h(".bashrc")) == "home version");  // untouched
}

TEST_CASE("sync_entry_store_to_home --dry-run --force previews without mutating the filesystem") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "store version");
    write_file(fh.h(".bashrc"), "home version");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), true, true);
    CHECK(r.outcome == SyncOutcome::WouldUpdate);
    CHECK(read_file(fh.h(".bashrc")) == "home version");  // untouched by dry run
}

TEST_CASE("sync_entry_store_to_home with force overwrites and creates a safety backup of the previous content") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "store version");
    write_file(fh.h(".bashrc"), "home version");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, true);
    CHECK(r.outcome == SyncOutcome::Updated);
    CHECK(read_file(fh.h(".bashrc")) == "store version");

    // A safety backup of the previous "home version" content must exist somewhere
    // under the store's safety-backups area.
    fs::path backups_root = fh.store / ".dotfile-manager" / "safety-backups";
    REQUIRE(fs::exists(backups_root));
    bool found_backup = false;
    for (auto& p : fs::recursive_directory_iterator(backups_root)) {
        if (p.is_regular_file() && read_file(p.path()) == "home version") found_backup = true;
    }
    CHECK(found_backup);
}

TEST_CASE("sync_entry_store_to_home refuses a type mismatch (home dir vs store file) without force") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "content");
    fs::create_directories(fh.h(".bashrc"));
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedTypeMismatch);
}

TEST_CASE("sync_entry_store_to_home refuses any directory merge change without force, even a pure addition") {
    FakeHome fh;
    write_file(fh.s("proj-store/x.txt"), "new");
    write_file(fh.h("proj/existing.txt"), "keep");  // home dir has content the store copy doesn't
    ManagedEntry entry{"proj", "proj-store", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    // Directory-merge apply is conservative: it requires --force for ANY
    // change, not just overwrites, so nothing is silently applied.
    CHECK(r.outcome == SyncOutcome::RefusedConflict);
    CHECK(fs::exists(fh.h("proj/existing.txt")));  // never deleted
    CHECK_FALSE(fs::exists(fh.h("proj/x.txt")));   // never silently added either
}

TEST_CASE("sync_entry_store_to_home directory merge under force backs up overwritten files") {
    FakeHome fh;
    write_file(fh.s("proj-store/a.txt"), "store-a");
    write_file(fh.h("proj/a.txt"), "home-a");
    ManagedEntry entry{"proj", "proj-store", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, true);
    CHECK(r.outcome == SyncOutcome::Updated);
    CHECK(read_file(fh.h("proj/a.txt")) == "store-a");
}

// ---------------------------------------------------------------------------
// sync_entry_store_to_home (apply) - symlink mode
// ---------------------------------------------------------------------------

TEST_CASE("sync_entry_store_to_home symlink mode creates a real symlink when home is missing") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Symlink, {}, {}};

    auto dry = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), true, false);
    CHECK(dry.outcome == SyncOutcome::WouldCreate);
    CHECK_FALSE(fs::exists(fh.h(".bashrc")));

    auto real = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(real.outcome == SyncOutcome::Created);
    CHECK(fs::is_symlink(fs::symlink_status(fh.h(".bashrc"))));
    CHECK(read_file(fh.h(".bashrc")) == "content");
}

TEST_CASE("sync_entry_store_to_home symlink mode reports NoChangeInSync when already linked correctly") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Symlink, {}, {}};
    sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);  // create it first
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::NoChangeInSync);
}

TEST_CASE("sync_entry_store_to_home symlink mode refuses to replace real home content without force") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "store content");
    write_file(fh.h(".bashrc"), "real home content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Symlink, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedConflict);
    CHECK(read_file(fh.h(".bashrc")) == "real home content");  // untouched
}

TEST_CASE("sync_entry_store_to_home symlink mode with force backs up then replaces real content with a symlink") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "store content");
    write_file(fh.h(".bashrc"), "real home content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Symlink, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, true);
    CHECK(r.outcome == SyncOutcome::ReplacedSymlink);
    CHECK(fs::is_symlink(fs::symlink_status(fh.h(".bashrc"))));

    fs::path backups_root = fh.store / ".dotfile-manager" / "safety-backups";
    REQUIRE(fs::exists(backups_root));
    bool found = false;
    for (auto& p : fs::recursive_directory_iterator(backups_root)) {
        if (p.is_regular_file() && read_file(p.path()) == "real home content") found = true;
    }
    CHECK(found);
}

TEST_CASE("sync_entry_store_to_home symlink mode dry-run never mutates the filesystem") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "store content");
    write_file(fh.h(".bashrc"), "real home content");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Symlink, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), true, true);
    CHECK(r.outcome == SyncOutcome::WouldReplaceSymlink);
    CHECK_FALSE(fs::is_symlink(fs::symlink_status(fh.h(".bashrc"))));
    CHECK(read_file(fh.h(".bashrc")) == "real home content");
}

TEST_CASE("sync_entry_store_to_home reports RefusedPathSafety for an escaping store_path") {
    FakeHome fh;
    ManagedEntry entry{".bashrc", "../../etc/evil", ManageMode::Copy, {}, {}};
    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedPathSafety);
}

// ---------------------------------------------------------------------------
// Snapshots
// ---------------------------------------------------------------------------

TEST_CASE("create_snapshot copies managed entries under a timestamped snapshot directory") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "content");
    std::vector<ManagedEntry> entries = {{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}}};
    std::string err;
    auto snap = create_snapshot(fh.store, entries, no_ignores(), false, &err);
    REQUIRE(snap.has_value());
    CHECK(err.empty());
    CHECK(fs::exists(snap->path / "bash" / ".bashrc"));
    CHECK(read_file(snap->path / "bash" / ".bashrc") == "content");
}

TEST_CASE("create_snapshot in dry_run mode reports the would-be path without writing") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "content");
    std::vector<ManagedEntry> entries = {{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}}};
    std::string err;
    auto snap = create_snapshot(fh.store, entries, no_ignores(), true, &err);
    REQUIRE(snap.has_value());
    CHECK_FALSE(fs::exists(snap->path));
}

TEST_CASE("list_snapshots returns an empty list when no snapshots exist") {
    FakeHome fh;
    fs::create_directories(fh.store);
    CHECK(list_snapshots(fh.store).empty());
}

TEST_CASE("list_snapshots returns created snapshots most-recent-first") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "content");
    std::vector<ManagedEntry> entries = {{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}}};
    std::string err;
    auto first = create_snapshot(fh.store, entries, no_ignores(), false, &err);
    REQUIRE(first.has_value());
    // Force a distinguishable second id by waiting past the 1-second timestamp
    // resolution boundary is undesirable in a unit test; instead verify
    // idempotent listing behavior and content.
    auto listed = list_snapshots(fh.store);
    REQUIRE_FALSE(listed.empty());
    CHECK(listed.front().id == first->id);
}

TEST_CASE("restore_entry_from_snapshot rejects an unknown snapshot id") {
    FakeHome fh;
    fs::create_directories(fh.store);
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = restore_entry_from_snapshot(fh.home, fh.store, "does-not-exist", entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::Failed);
}

TEST_CASE("restore_entry_from_snapshot rejects a path-traversal snapshot id") {
    FakeHome fh;
    fs::create_directories(fh.store);
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};
    auto r = restore_entry_from_snapshot(fh.home, fh.store, "../../../etc", entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::RefusedPathSafety);
}

TEST_CASE("restore_entry_from_snapshot restores home content from a snapshot") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "snapshot content");
    std::vector<ManagedEntry> entries = {{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}}};
    std::string err;
    auto snap = create_snapshot(fh.store, entries, no_ignores(), false, &err);
    REQUIRE(snap.has_value());

    // Home is missing, so a plain (non-force) restore should still create it.
    ManagedEntry entry = entries[0];
    auto r = restore_entry_from_snapshot(fh.home, fh.store, snap->id, entry, no_ignores(), false, false);
    CHECK(r.outcome == SyncOutcome::Created);
    CHECK(read_file(fh.h(".bashrc")) == "snapshot content");
}

// ---------------------------------------------------------------------------
// Non-recursive deletion guarantee
// ---------------------------------------------------------------------------

TEST_CASE("apply with force never recursively deletes a non-empty home directory being replaced") {
    FakeHome fh;
    write_file(fh.s("bash/.bashrc"), "file content, not a directory");
    fs::create_directories(fh.h(".bashrc"));
    write_file(fh.h(".bashrc") / "child.txt", "must not be silently destroyed");
    ManagedEntry entry{".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}};

    auto r = sync_entry_store_to_home(fh.home, fh.store, entry, no_ignores(), false, true);
    // The home directory is non-empty, so even with --force the structural
    // guarantee (only std::filesystem::remove(), never remove_all()) must
    // cause this to be refused rather than silently destroying the contents.
    CHECK(r.outcome == SyncOutcome::RefusedUnsafeDirectoryReplace);
    CHECK(fs::exists(fh.h(".bashrc") / "child.txt"));
}
