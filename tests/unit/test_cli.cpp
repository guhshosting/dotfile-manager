// test_cli.cpp - unit tests for dfm/cli.hpp (argument parsing plus
// resolve_user_path_to_home_relative, which is declared in commands.hpp but
// implemented in cli.cpp).
// SPDX-License-Identifier: MIT
#include "dfm/cli.hpp"

#include "dfm/commands.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

#include <algorithm>

using namespace dfm;
using namespace dfm_test;

namespace {

// Parses a list of CLI argument strings (NOT including argv[0]) by
// constructing a real argv array, keeping the backing strings alive for the
// duration of the call.
ArgParseResult parse(const std::vector<std::string>& args) {
    std::vector<std::string> owned;
    owned.push_back("dotfile-manager");
    for (auto& a : args) owned.push_back(a);

    std::vector<char*> argv;
    for (auto& s : owned) argv.push_back(const_cast<char*>(s.c_str()));

    return parse_args(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

// ---------------------------------------------------------------------------
// known_commands / command_accepts_force / command_accepts_dry_run
// ---------------------------------------------------------------------------

TEST_CASE("known_commands includes every documented command") {
    auto cmds = known_commands();
    for (const char* c : {"discover", "list", "add", "remove", "status", "diff", "backup", "apply", "restore",
                           "check", "report", "version"}) {
        CHECK(std::find(cmds.begin(), cmds.end(), c) != cmds.end());
    }
}

TEST_CASE("command_accepts_force is true only for add/backup/apply/restore") {
    CHECK(command_accepts_force("add"));
    CHECK(command_accepts_force("backup"));
    CHECK(command_accepts_force("apply"));
    CHECK(command_accepts_force("restore"));
    CHECK_FALSE(command_accepts_force("discover"));
    CHECK_FALSE(command_accepts_force("list"));
    CHECK_FALSE(command_accepts_force("remove"));
    CHECK_FALSE(command_accepts_force("status"));
    CHECK_FALSE(command_accepts_force("diff"));
    CHECK_FALSE(command_accepts_force("check"));
    CHECK_FALSE(command_accepts_force("report"));
    CHECK_FALSE(command_accepts_force("version"));
}

TEST_CASE("command_accepts_dry_run is true only for add/remove/backup/apply/restore") {
    CHECK(command_accepts_dry_run("add"));
    CHECK(command_accepts_dry_run("remove"));
    CHECK(command_accepts_dry_run("backup"));
    CHECK(command_accepts_dry_run("apply"));
    CHECK(command_accepts_dry_run("restore"));
    CHECK_FALSE(command_accepts_dry_run("discover"));
    CHECK_FALSE(command_accepts_dry_run("status"));
    CHECK_FALSE(command_accepts_dry_run("diff"));
    CHECK_FALSE(command_accepts_dry_run("check"));
    CHECK_FALSE(command_accepts_dry_run("report"));
    CHECK_FALSE(command_accepts_dry_run("version"));
}

// ---------------------------------------------------------------------------
// parse_args
// ---------------------------------------------------------------------------

TEST_CASE("parse_args with no tokens shows help and is not ok") {
    auto r = parse({});
    CHECK_FALSE(r.ok);
    CHECK(r.show_help);
}

TEST_CASE("parse_args recognizes --help") {
    auto r = parse({"--help"});
    CHECK(r.ok);
    CHECK(r.show_help);
}

TEST_CASE("parse_args recognizes -h") {
    auto r = parse({"-h"});
    CHECK(r.ok);
    CHECK(r.show_help);
}

TEST_CASE("parse_args recognizes --version") {
    auto r = parse({"--version"});
    CHECK(r.ok);
    CHECK(r.show_version);
}

TEST_CASE("parse_args rejects an unknown command") {
    auto r = parse({"frobnicate"});
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("parse_args parses a simple command with a positional argument") {
    auto r = parse({"add", ".bashrc"});
    REQUIRE(r.ok);
    CHECK(r.args.command == "add");
    REQUIRE(r.args.positional.size() == 1);
    CHECK(r.args.positional[0] == ".bashrc");
}

TEST_CASE("parse_args parses --format with a valid value") {
    auto r = parse({"status", "--format", "json"});
    REQUIRE(r.ok);
    CHECK(r.args.global.format == OutputFormat::Json);
}

TEST_CASE("parse_args parses --format=value syntax") {
    auto r = parse({"status", "--format=markdown"});
    REQUIRE(r.ok);
    CHECK(r.args.global.format == OutputFormat::Markdown);
}

TEST_CASE("parse_args rejects an invalid --format value") {
    auto r = parse({"status", "--format", "yaml"});
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_args rejects --format with a missing value") {
    auto r = parse({"status", "--format"});
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_args parses --output as a file path") {
    auto r = parse({"status", "--output", "/tmp/out.txt"});
    REQUIRE(r.ok);
    REQUIRE(r.args.global.output_file.has_value());
    CHECK(*r.args.global.output_file == fs::path("/tmp/out.txt"));
}

TEST_CASE("parse_args parses --verbose, --quiet, --no-color as flags") {
    auto r = parse({"status", "--verbose", "--quiet", "--no-color"});
    REQUIRE(r.ok);
    CHECK(r.args.global.verbose);
    CHECK(r.args.global.quiet);
    CHECK(r.args.global.no_color);
}

TEST_CASE("parse_args accepts --dry-run for a command that supports it") {
    auto r = parse({"backup", "--dry-run"});
    REQUIRE(r.ok);
    CHECK(r.args.global.dry_run);
}

TEST_CASE("parse_args rejects --dry-run for a command that does not support it (e.g. discover)") {
    auto r = parse({"discover", "--dry-run"});
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("parse_args accepts --force for a command that supports it") {
    auto r = parse({"apply", "--force"});
    REQUIRE(r.ok);
    CHECK(r.args.global.force);
}

TEST_CASE("parse_args rejects --force for discover") {
    auto r = parse({"discover", "--force"});
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_args rejects --force for status") {
    auto r = parse({"status", "--force"});
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_args parses add-specific options: --mode, --store-path, --tag, --allow-sensitive") {
    auto r = parse({"add", ".bashrc", "--mode", "symlink", "--store-path", "bash/.bashrc", "--tag", "shell",
                     "--tag", "posix", "--allow-sensitive"});
    REQUIRE(r.ok);
    CHECK(r.args.options.at("mode") == "symlink");
    CHECK(r.args.options.at("store-path") == "bash/.bashrc");
    CHECK(r.args.options.at("tags") == "shell,posix");
    CHECK(r.args.flags.count("allow-sensitive") == 1);
}

TEST_CASE("parse_args rejects add-specific options for other commands") {
    auto r = parse({"status", "--mode", "symlink"});
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_args parses --purge-store for remove") {
    auto r = parse({"remove", ".bashrc", "--purge-store"});
    REQUIRE(r.ok);
    CHECK(r.args.flags.count("purge-store") == 1);
}

TEST_CASE("parse_args parses --snapshot for backup") {
    auto r = parse({"backup", "--snapshot"});
    REQUIRE(r.ok);
    CHECK(r.args.flags.count("snapshot") == 1);
}

TEST_CASE("parse_args parses --snapshot-id for restore") {
    auto r = parse({"restore", "--snapshot-id", "latest"});
    REQUIRE(r.ok);
    CHECK(r.args.options.at("snapshot-id") == "latest");
}

TEST_CASE("parse_args parses --snapshots for list") {
    auto r = parse({"list", "--snapshots"});
    REQUIRE(r.ok);
    CHECK(r.args.flags.count("snapshots") == 1);
}

TEST_CASE("parse_args rejects an unknown long option") {
    auto r = parse({"status", "--bogus-flag"});
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_args rejects an unknown short option") {
    auto r = parse({"status", "-x"});
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_args treats a bare dash as a positional argument") {
    auto r = parse({"add", "-"});
    REQUIRE(r.ok);
    REQUIRE(r.args.positional.size() == 1);
    CHECK(r.args.positional[0] == "-");
}

TEST_CASE("parse_args collects multiple positional arguments") {
    auto r = parse({"status", ".bashrc", ".vimrc"});
    REQUIRE(r.ok);
    REQUIRE(r.args.positional.size() == 2);
}

// ---------------------------------------------------------------------------
// resolve_user_path_to_home_relative
// ---------------------------------------------------------------------------

TEST_CASE("resolve_user_path_to_home_relative resolves a simple relative dotfile name") {
    FakeHome fh;
    write_file(fh.h(".bashrc"), "x");
    std::string err;
    auto rel = resolve_user_path_to_home_relative(fh.home, ".bashrc", &err);
    REQUIRE(rel.has_value());
    CHECK(*rel == ".bashrc");
}

TEST_CASE("resolve_user_path_to_home_relative expands a leading ~/ shortcut") {
    FakeHome fh;
    write_file(fh.h(".vimrc"), "x");
    std::string err;
    auto rel = resolve_user_path_to_home_relative(fh.home, "~/.vimrc", &err);
    REQUIRE(rel.has_value());
    CHECK(*rel == ".vimrc");
}

TEST_CASE("resolve_user_path_to_home_relative resolves an absolute path under home") {
    FakeHome fh;
    write_file(fh.h(".gitconfig"), "x");
    std::string err;
    auto rel = resolve_user_path_to_home_relative(fh.home, (fh.home / ".gitconfig").string(), &err);
    REQUIRE(rel.has_value());
    CHECK(*rel == ".gitconfig");
}

TEST_CASE("resolve_user_path_to_home_relative rejects a path outside home") {
    FakeHome fh;
    std::string err;
    auto rel = resolve_user_path_to_home_relative(fh.home, "/etc/hostname", &err);
    CHECK_FALSE(rel.has_value());
    CHECK_FALSE(err.empty());
}

TEST_CASE("resolve_user_path_to_home_relative rejects a traversal that escapes home") {
    FakeHome fh;
    std::string err;
    auto rel = resolve_user_path_to_home_relative(fh.home, "../../../etc/passwd", &err);
    CHECK_FALSE(rel.has_value());
}

TEST_CASE("resolve_user_path_to_home_relative rejects home itself (empty relative path)") {
    FakeHome fh;
    std::string err;
    auto rel = resolve_user_path_to_home_relative(fh.home, ".", &err);
    CHECK_FALSE(rel.has_value());
}

TEST_CASE("resolve_user_path_to_home_relative resolves a nested config path") {
    FakeHome fh;
    write_file(fh.h(".config/htop/htoprc"), "x");
    std::string err;
    auto rel = resolve_user_path_to_home_relative(fh.home, ".config/htop/htoprc", &err);
    REQUIRE(rel.has_value());
    CHECK(*rel == ".config/htop/htoprc");
}
