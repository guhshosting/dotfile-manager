// test_commands.cpp - functional tests of the cmd_* handlers, driven by
// directly-constructed AppContext/ParsedArgs (bypassing parse_args and
// dispatch, but exercising the exact command logic in src/commands.cpp).
// Every test runs against a FakeHome; never touches the real HOME.
// SPDX-License-Identifier: MIT
#include "dfm/commands.hpp"

#include "dfm/backup.hpp"
#include "dfm/config.hpp"
#include "dfm/exit_codes.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

using namespace dfm;
using namespace dfm_test;

namespace {

// Builds a fresh AppContext pointing at a FakeHome's home/store dirs, with an
// empty config and no ignore patterns (unless the caller adds them). The
// store directory is not created on disk unless something writes into it -
// callers can flip store_exists after creating it if a test needs that.
AppContext make_ctx(FakeHome& fh) {
    AppContext ctx;
    ctx.home_dir = fh.home;
    ctx.config_path = fh.root.path() / "config.json";
    ctx.config.store = fh.store.string();  // matches dispatch()'s default-store-fill behavior
    ctx.store_dir = fh.store;
    ctx.store_exists = fs::exists(fh.store);
    ctx.global = GlobalOptions{};
    return ctx;
}

ParsedArgs args_for(const std::string& command, std::vector<std::string> positional = {}) {
    ParsedArgs a;
    a.command = command;
    a.positional = std::move(positional);
    return a;
}

}  // namespace

// ---------------------------------------------------------------------------
// discover
// ---------------------------------------------------------------------------

TEST_CASE("cmd_discover rejects positional arguments with usage error") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto args = args_for("discover", {"foo"});
    auto r = cmd_discover(ctx, args);
    CHECK(r.exit_code == kExitUsageError);
}

TEST_CASE("cmd_discover reports bounded dotfiles and flags sensitive ones") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    write_file(fh.h(".ssh/id_rsa"), "secret");
    auto ctx = make_ctx(fh);
    auto args = args_for("discover");
    auto r = cmd_discover(ctx, args);
    CHECK(r.exit_code == kExitSuccess);
    CHECK(r.output.find(".bashrc") != std::string::npos);
    CHECK(r.output.find("SENSITIVE") != std::string::npos);
}

TEST_CASE("cmd_discover never touches the filesystem (no store created)") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    auto args = args_for("discover");
    cmd_discover(ctx, args);
    CHECK_FALSE(fs::exists(fh.store));
}

// ---------------------------------------------------------------------------
// add
// ---------------------------------------------------------------------------

TEST_CASE("cmd_add adds a plain file in copy mode and writes config") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "export PATH=$PATH");
    auto ctx = make_ctx(fh);
    auto args = args_for("add", {".bashrc"});
    auto r = cmd_add(ctx, args);
    CHECK(r.exit_code == kExitSuccess);
    REQUIRE(ctx.config.entries.size() == 1);
    CHECK(ctx.config.entries[0].home_path == ".bashrc");
    CHECK(ctx.config.entries[0].mode == ManageMode::Copy);
    CHECK(fs::exists(ctx.config_path));
    CHECK(read_file(fh.s(".bashrc")) == "export PATH=$PATH");
}

TEST_CASE("cmd_add in symlink mode creates a real symlink at home pointing into the store") {
    FakeHome fh;
    write_file(fh.h(".vimrc"), "set nu");
    auto ctx = make_ctx(fh);
    ParsedArgs args = args_for("add", {".vimrc"});
    args.options["mode"] = "symlink";
    auto r = cmd_add(ctx, args);
    CHECK(r.exit_code == kExitSuccess);
    REQUIRE(ctx.config.entries.size() == 1);
    CHECK(ctx.config.entries[0].mode == ManageMode::Symlink);
}

TEST_CASE("cmd_add rejects a nonexistent path with usage error") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto args = args_for("add", {".does-not-exist"});
    auto r = cmd_add(ctx, args);
    CHECK(r.exit_code == kExitUsageError);
    CHECK(ctx.config.entries.empty());
}

TEST_CASE("cmd_add rejects a sensitive path without --allow-sensitive") {
    FakeHome fh;
    write_file(fh.h(".ssh/id_rsa"), "secret");
    auto ctx = make_ctx(fh);
    auto args = args_for("add", {".ssh/id_rsa"});
    auto r = cmd_add(ctx, args);
    CHECK(r.exit_code == kExitPathSafetyError);
    CHECK(ctx.config.entries.empty());
    CHECK_FALSE(fs::exists(fh.store));  // nothing copied
}

TEST_CASE("cmd_add accepts a sensitive path when --allow-sensitive is given") {
    FakeHome fh;
    write_file(fh.h(".ssh/id_rsa"), "secret");
    auto ctx = make_ctx(fh);
    ParsedArgs args = args_for("add", {".ssh/id_rsa"});
    args.flags.insert("allow-sensitive");
    auto r = cmd_add(ctx, args);
    CHECK(r.exit_code == kExitSuccess);
    CHECK(ctx.config.entries.size() == 1);
}

TEST_CASE("cmd_add rejects adding an already-managed path (duplicate)") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    auto args = args_for("add", {".bashrc"});
    REQUIRE(cmd_add(ctx, args).exit_code == kExitSuccess);
    auto r2 = cmd_add(ctx, args);
    CHECK(r2.exit_code == kExitUnresolvedConflict);
    CHECK(ctx.config.entries.size() == 1);  // not duplicated
}

TEST_CASE("cmd_add rejects a store-path collision with a different home path") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    write_file(fh.h(".zshrc"), "y");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);

    ParsedArgs args2 = args_for("add", {".zshrc"});
    args2.options["store-path"] = ctx.config.entries[0].store_path;  // collide
    auto r2 = cmd_add(ctx, args2);
    CHECK(r2.exit_code == kExitUnresolvedConflict);
    CHECK(ctx.config.entries.size() == 1);
}

TEST_CASE("cmd_add rejects an invalid --mode value") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    ParsedArgs args = args_for("add", {".bashrc"});
    args.options["mode"] = "bogus";
    auto r = cmd_add(ctx, args);
    CHECK(r.exit_code == kExitUsageError);
}

TEST_CASE("cmd_add --dry-run does not create the store or modify config") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    ctx.global.dry_run = true;
    auto args = args_for("add", {".bashrc"});
    auto r = cmd_add(ctx, args);
    CHECK(r.exit_code == kExitSuccess);
    CHECK(r.output.find("DRY RUN") != std::string::npos);
    CHECK(ctx.config.entries.empty());
    CHECK_FALSE(fs::exists(fh.store));
    CHECK_FALSE(fs::exists(ctx.config_path));
}

TEST_CASE("cmd_add requires exactly one positional argument") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto r0 = cmd_add(ctx, args_for("add", {}));
    CHECK(r0.exit_code == kExitUsageError);
    auto r2 = cmd_add(ctx, args_for("add", {".a", ".b"}));
    CHECK(r2.exit_code == kExitUsageError);
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

TEST_CASE("cmd_remove drops management metadata but never deletes the home file") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);

    auto r = cmd_remove(ctx, args_for("remove", {".bashrc"}));
    CHECK(r.exit_code == kExitSuccess);
    CHECK(ctx.config.entries.empty());
    CHECK(fs::exists(fh.h(".bashrc")));      // home file untouched
    CHECK(fs::exists(fh.s(".bashrc")));      // store copy untouched (no --purge-store)
}

TEST_CASE("cmd_remove rejects removing a path that is not managed") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto r = cmd_remove(ctx, args_for("remove", {".not-managed"}));
    CHECK(r.exit_code == kExitUsageError);
}

TEST_CASE("cmd_remove --purge-store deletes a regular store file but reports non-empty directories intact") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    write_file(fh.h("proj/keep.txt"), "keep");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    REQUIRE(cmd_add(ctx, args_for("add", {"proj"})).exit_code == kExitSuccess);

    ParsedArgs rm1 = args_for("remove", {".bashrc"});
    rm1.flags.insert("purge-store");
    auto r1 = cmd_remove(ctx, rm1);
    CHECK(r1.exit_code == kExitSuccess);
    CHECK_FALSE(fs::exists(fh.s(".bashrc")));  // store copy purged

    ParsedArgs rm2 = args_for("remove", {"proj"});
    rm2.flags.insert("purge-store");
    auto r2 = cmd_remove(ctx, rm2);
    CHECK(r2.exit_code == kExitDifferences);  // non-empty dir: not deleted, reported
    CHECK(fs::exists(fh.s("proj/keep.txt")));  // still there
}

TEST_CASE("cmd_remove --dry-run makes no changes") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    ctx.global.dry_run = true;
    auto r = cmd_remove(ctx, args_for("remove", {".bashrc"}));
    CHECK(r.exit_code == kExitSuccess);
    CHECK(ctx.config.entries.size() == 1);  // still managed
}

// ---------------------------------------------------------------------------
// status / diff
// ---------------------------------------------------------------------------

TEST_CASE("cmd_status reports IN_SYNC right after add, exit 0") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    auto r = cmd_status(ctx, args_for("status"));
    CHECK(r.exit_code == kExitSuccess);
    CHECK(r.output.find("IN_SYNC") != std::string::npos);
}

TEST_CASE("cmd_status reports MODIFIED and exit 1 after home file changes") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    write_file(fh.h(".bashrc"), "changed");
    auto r = cmd_status(ctx, args_for("status"));
    CHECK(r.exit_code == kExitDifferences);
    CHECK(r.output.find("MODIFIED") != std::string::npos);
}

TEST_CASE("cmd_status --format json produces parseable JSON") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    ctx.global.format = OutputFormat::Json;
    auto r = cmd_status(ctx, args_for("status"));
    CHECK(r.output.front() == '{');
    CHECK(r.output.find("\"IN_SYNC\"") != std::string::npos);
}

TEST_CASE("cmd_status rejects an unmanaged positional path") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto r = cmd_status(ctx, args_for("status", {".not-managed"}));
    CHECK(r.exit_code == kExitUsageError);
}

TEST_CASE("cmd_diff requires exactly one path argument") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto r = cmd_diff(ctx, args_for("diff", {}));
    CHECK(r.exit_code == kExitUsageError);
}

TEST_CASE("cmd_diff reports no differences for an in-sync entry") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    auto r = cmd_diff(ctx, args_for("diff", {".bashrc"}));
    CHECK(r.exit_code == kExitSuccess);
    CHECK(r.output.find("No differences") != std::string::npos);
}

TEST_CASE("cmd_diff shows a unified diff for a modified text file") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "line1\nline2\n");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    write_file(fh.h(".bashrc"), "line1\nCHANGED\n");
    auto r = cmd_diff(ctx, args_for("diff", {".bashrc"}));
    CHECK(r.exit_code == kExitDifferences);
    CHECK(r.output.find("CHANGED") != std::string::npos);
}

// ---------------------------------------------------------------------------
// backup / apply
// ---------------------------------------------------------------------------

TEST_CASE("cmd_backup syncs a modified home file into the store") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    write_file(fh.h(".bashrc"), "updated");
    auto r = cmd_backup(ctx, args_for("backup"));
    CHECK(r.exit_code == kExitSuccess);
    CHECK(read_file(fh.s(".bashrc")) == "updated");
}

TEST_CASE("cmd_backup --dry-run previews without writing to the store") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    write_file(fh.h(".bashrc"), "updated");
    ctx.global.dry_run = true;
    auto r = cmd_backup(ctx, args_for("backup"));
    CHECK(r.exit_code == kExitDifferences);
    CHECK(read_file(fh.s(".bashrc")) == "x");  // unchanged
}

TEST_CASE("cmd_apply refuses to overwrite differing home content without --force") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "home-version");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    write_file(fh.s(".bashrc"), "store-version");  // diverge after add
    auto r = cmd_apply(ctx, args_for("apply"));
    CHECK(r.exit_code == kExitUnresolvedConflict);
    CHECK(read_file(fh.h(".bashrc")) == "home-version");  // untouched
}

TEST_CASE("cmd_apply with --force overwrites home content and creates a safety backup") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "home-version");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    write_file(fh.s(".bashrc"), "store-version");
    ctx.global.force = true;
    auto r = cmd_apply(ctx, args_for("apply"));
    CHECK(r.exit_code == kExitSuccess);
    CHECK(read_file(fh.h(".bashrc")) == "store-version");
    CHECK(fs::exists(fh.store / ".dotfile-manager" / "safety-backups"));
}

TEST_CASE("cmd_apply on a fresh store with no store directory reports nothing to apply") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto r = cmd_apply(ctx, args_for("apply"));
    CHECK(r.exit_code == kExitDifferences);
}

// ---------------------------------------------------------------------------
// restore
// ---------------------------------------------------------------------------

TEST_CASE("cmd_restore requires --snapshot-id") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    fs::create_directories(fh.store);
    ctx.store_exists = true;
    auto r = cmd_restore(ctx, args_for("restore"));
    CHECK(r.exit_code == kExitUsageError);
}

TEST_CASE("cmd_restore --snapshot-id latest restores from the most recent snapshot") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "version-1");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);

    ParsedArgs backup_args = args_for("backup");
    backup_args.flags.insert("snapshot");
    write_file(fh.h(".bashrc"), "version-2");
    REQUIRE(cmd_backup(ctx, backup_args).exit_code == kExitSuccess);

    write_file(fh.h(".bashrc"), "version-3-uncommitted");
    ParsedArgs restore_args = args_for("restore");
    restore_args.options["snapshot-id"] = "latest";
    restore_args.global.force = true;
    ctx.global.force = true;
    auto r = cmd_restore(ctx, restore_args);
    CHECK(r.exit_code == kExitSuccess);
    CHECK(read_file(fh.h(".bashrc")) == "version-1");
}

// ---------------------------------------------------------------------------
// check / report
// ---------------------------------------------------------------------------

TEST_CASE("cmd_check reports OK for a fresh empty config") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto r = cmd_check(ctx, args_for("check"));
    CHECK(r.exit_code == kExitDifferences);  // warns: store does not exist yet
    CHECK(r.output.find("OK") != std::string::npos);
}

TEST_CASE("cmd_check rejects positional arguments") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto r = cmd_check(ctx, args_for("check", {"x"}));
    CHECK(r.exit_code == kExitUsageError);
}

TEST_CASE("cmd_report rejects positional arguments") {
    FakeHome fh;
    auto ctx = make_ctx(fh);
    auto r = cmd_report(ctx, args_for("report", {"x"}));
    CHECK(r.exit_code == kExitUsageError);
}

TEST_CASE("cmd_report combines status and check output") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    auto ctx = make_ctx(fh);
    REQUIRE(cmd_add(ctx, args_for("add", {".bashrc"})).exit_code == kExitSuccess);
    auto r = cmd_report(ctx, args_for("report"));
    CHECK(r.output.find("Report") != std::string::npos);
    CHECK(r.output.find(".bashrc") != std::string::npos);
}
