// test_path_safety.cpp - unit tests for dfm/path_safety.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/path_safety.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

using namespace dfm;
using namespace dfm_test;

// ---------------------------------------------------------------------------
// validate_relative_path
// ---------------------------------------------------------------------------

TEST_CASE("validate_relative_path accepts a simple relative path") {
    auto r = validate_relative_path(".bashrc");
    CHECK(r.ok());
    CHECK(r.resolved == fs::path(".bashrc"));
}

TEST_CASE("validate_relative_path accepts a nested relative path") {
    auto r = validate_relative_path("config/nvim/init.vim");
    CHECK(r.ok());
}

TEST_CASE("validate_relative_path rejects an empty string") {
    auto r = validate_relative_path("");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::Empty);
}

TEST_CASE("validate_relative_path rejects an absolute path") {
    auto r = validate_relative_path("/etc/passwd");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::Absolute);
}

TEST_CASE("validate_relative_path rejects a leading '..' traversal") {
    auto r = validate_relative_path("../../../etc/passwd");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::ParentTraversal);
}

TEST_CASE("validate_relative_path rejects an embedded '..' component") {
    auto r = validate_relative_path("config/../../secret");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::ParentTraversal);
}

TEST_CASE("validate_relative_path rejects a trailing '..' component") {
    auto r = validate_relative_path("config/..");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::ParentTraversal);
}

TEST_CASE("validate_relative_path rejects a bare '..'") {
    auto r = validate_relative_path("..");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::ParentTraversal);
}

TEST_CASE("validate_relative_path rejects an embedded NUL byte") {
    std::string bad = "config/bad";
    bad.push_back('\0');
    bad += "name";
    auto r = validate_relative_path(bad);
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::NullByte);
}

TEST_CASE("validate_relative_path normalizes a doubled separator instead of treating it as an empty component") {
    // std::filesystem::path iteration collapses repeated separators
    // (POSIX generic-format behavior), so "config//nvim" never actually
    // produces an empty path component here; it normalizes safely to
    // "config/nvim". The EmptyComponent error path exists defensively but
    // is not reachable via doubled separators on this platform/library.
    auto r = validate_relative_path("config//nvim");
    CHECK(r.ok());
    CHECK(r.resolved == fs::path("config/nvim"));
}

TEST_CASE("validate_relative_path treats a lone '.' as not a usable managed path") {
    auto r = validate_relative_path(".");
    // "." normalizes to an empty relative path, which is not a usable entry.
    CHECK_FALSE(r.ok());
}

TEST_CASE("validate_relative_path accepts a dotted filename that is not traversal") {
    auto r = validate_relative_path("..bashrc_backup");
    CHECK(r.ok());
}

TEST_CASE("to_string(PathSafetyError) never returns an empty string") {
    for (auto err : {PathSafetyError::None, PathSafetyError::Empty, PathSafetyError::Absolute,
                      PathSafetyError::ParentTraversal, PathSafetyError::NullByte, PathSafetyError::EmptyComponent,
                      PathSafetyError::EscapesBase, PathSafetyError::SymlinkEscapesBase,
                      PathSafetyError::FilesystemLoop, PathSafetyError::ParentNotDirectory,
                      PathSafetyError::BrokenIntermediateSymlink, PathSafetyError::CanonicalizeFailed,
                      PathSafetyError::IsRoot}) {
        CHECK_FALSE(to_string(err).empty());
    }
}

// ---------------------------------------------------------------------------
// try_canonical / try_weakly_canonical
// ---------------------------------------------------------------------------

TEST_CASE("try_canonical succeeds for an existing directory") {
    TempDir tmp;
    auto c = try_canonical(tmp.path());
    REQUIRE(c.has_value());
    CHECK(fs::equivalent(*c, tmp.path()));
}

TEST_CASE("try_canonical fails for a nonexistent path") {
    TempDir tmp;
    auto c = try_canonical(tmp.path() / "does-not-exist");
    CHECK_FALSE(c.has_value());
}

TEST_CASE("try_weakly_canonical succeeds for a path whose leaf does not yet exist") {
    TempDir tmp;
    auto c = try_weakly_canonical(tmp.path() / "not-yet-created.txt");
    REQUIRE(c.has_value());
    CHECK(c->parent_path() == tmp.path());
}

TEST_CASE("try_weakly_canonical fails gracefully for a circular symlink prefix") {
    TempDir tmp;
    make_symlink(tmp.path() / "b", tmp.path() / "a");
    make_symlink(tmp.path() / "a", tmp.path() / "b");
    auto c = try_weakly_canonical(tmp.path() / "a" / "child.txt");
    CHECK_FALSE(c.has_value());
}

// ---------------------------------------------------------------------------
// is_within
// ---------------------------------------------------------------------------

TEST_CASE("is_within is true for the base directory itself") {
    fs::path base = "/home/user";
    CHECK(is_within(base, base));
}

TEST_CASE("is_within is true for a descendant path") {
    CHECK(is_within(fs::path("/home/user"), fs::path("/home/user/.config/nvim")));
}

TEST_CASE("is_within is false for a sibling directory") {
    CHECK_FALSE(is_within(fs::path("/home/user"), fs::path("/home/other")));
}

TEST_CASE("is_within is false for a prefix look-alike directory name") {
    // "/home/user2" is NOT within "/home/user" even though it shares a
    // string prefix - must be a real path-component descendant.
    CHECK_FALSE(is_within(fs::path("/home/user"), fs::path("/home/user2")));
}

TEST_CASE("is_within is false for the parent of the base") {
    CHECK_FALSE(is_within(fs::path("/home/user/.config"), fs::path("/home/user")));
}

// ---------------------------------------------------------------------------
// resolve_within - malicious and edge cases
// ---------------------------------------------------------------------------

TEST_CASE("resolve_within accepts a simple existing relative file") {
    TempDir tmp;
    write_file(tmp.path() / ".bashrc", "content");
    auto r = resolve_within(tmp.path(), ".bashrc");
    REQUIRE(r.ok());
    CHECK(fs::equivalent(r.resolved, tmp.path() / ".bashrc"));
}

TEST_CASE("resolve_within accepts a relative path that does not exist yet") {
    TempDir tmp;
    auto r = resolve_within(tmp.path(), "new-file.txt");
    CHECK(r.ok());
}

TEST_CASE("resolve_within rejects '../../../etc/passwd'") {
    TempDir tmp;
    auto r = resolve_within(tmp.path(), "../../../etc/passwd");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::ParentTraversal);
}

TEST_CASE("resolve_within rejects an absolute relative-field value") {
    TempDir tmp;
    auto r = resolve_within(tmp.path(), "/etc/passwd");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::Absolute);
}

TEST_CASE("resolve_within rejects a symlink inside the base pointing outside it") {
    TempDir tmp;
    TempDir outside;
    fs::create_directories(tmp.path() / "sub");
    make_symlink(outside.path(), tmp.path() / "sub" / "escape");
    auto r = resolve_within(tmp.path(), "sub/escape/payload.txt");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::SymlinkEscapesBase);
}

TEST_CASE("resolve_within permits a leaf that is itself a symlink (not dereferenced)") {
    TempDir tmp;
    TempDir outside;
    make_symlink(outside.path() / "target", tmp.path() / "leaf-link");
    auto r = resolve_within(tmp.path(), "leaf-link");
    // The leaf itself is allowed to resolve lexically; callers decide how to
    // treat an existing symlink leaf. It must NOT be silently followed here.
    CHECK(r.ok());
    CHECK(r.resolved.filename() == "leaf-link");
}

TEST_CASE("resolve_within detects a circular intermediate symlink") {
    TempDir tmp;
    make_symlink(tmp.path() / "loop_b", tmp.path() / "loop_a");
    make_symlink(tmp.path() / "loop_a", tmp.path() / "loop_b");
    auto r = resolve_within(tmp.path(), "loop_a/child.txt");
    CHECK_FALSE(r.ok());
    CHECK((r.error == PathSafetyError::FilesystemLoop || r.error == PathSafetyError::BrokenIntermediateSymlink));
}

TEST_CASE("resolve_within reports a dangling intermediate symlink distinctly from success") {
    TempDir tmp;
    make_symlink(tmp.path() / "does-not-exist-target", tmp.path() / "dangling");
    auto r = resolve_within(tmp.path(), "dangling/child.txt");
    CHECK_FALSE(r.ok());
}

TEST_CASE("resolve_within rejects an empty relative path") {
    TempDir tmp;
    auto r = resolve_within(tmp.path(), "");
    CHECK_FALSE(r.ok());
}

TEST_CASE("resolve_within allows deeply nested existing directories within base") {
    TempDir tmp;
    fs::create_directories(tmp.path() / "a" / "b" / "c");
    auto r = resolve_within(tmp.path(), "a/b/c/d.txt");
    CHECK(r.ok());
}

TEST_CASE("resolve_within rejects when an intermediate component is a regular file, not a directory") {
    TempDir tmp;
    write_file(tmp.path() / "notadir", "x");
    auto r = resolve_within(tmp.path(), "notadir/child.txt");
    // resolve_within actively detects that an intermediate path component
    // is a regular file (not a directory) and refuses rather than silently
    // resolving further underneath it.
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::ParentNotDirectory);
}

TEST_CASE("store entry resolving outside store via crafted store_path is rejected") {
    TempDir store;
    auto r = resolve_within(store.path(), "../../etc/evilfile");
    CHECK_FALSE(r.ok());
}

TEST_CASE("home symlink escaping HOME is rejected when resolving a discovered path") {
    TempDir home;
    TempDir outside;
    make_dir_symlink(outside.path(), home.path() / ".config-escape");
    auto r = resolve_within(home.path(), ".config-escape/secret");
    CHECK_FALSE(r.ok());
    CHECK(r.error == PathSafetyError::SymlinkEscapesBase);
}

// ---------------------------------------------------------------------------
// ensure_directory_canonical
// ---------------------------------------------------------------------------

TEST_CASE("ensure_directory_canonical returns the canonical path for an existing directory") {
    TempDir tmp;
    std::string err;
    auto r = ensure_directory_canonical(tmp.path(), false, &err);
    REQUIRE(r.has_value());
    CHECK(fs::equivalent(*r, tmp.path()));
}

TEST_CASE("ensure_directory_canonical refuses to create when create_if_missing is false") {
    TempDir tmp;
    std::string err;
    auto r = ensure_directory_canonical(tmp.path() / "missing", false, &err);
    CHECK_FALSE(r.has_value());
    CHECK_FALSE(err.empty());
}

TEST_CASE("ensure_directory_canonical creates a single missing directory level") {
    TempDir tmp;
    std::string err;
    auto r = ensure_directory_canonical(tmp.path() / "newdir", true, &err);
    REQUIRE(r.has_value());
    CHECK(fs::is_directory(*r));
}

TEST_CASE("ensure_directory_canonical creates multiple missing nested directory levels") {
    TempDir tmp;
    std::string err;
    auto r = ensure_directory_canonical(tmp.path() / "a" / "b" / "c", true, &err);
    REQUIRE(r.has_value());
    CHECK(fs::is_directory(*r));
    CHECK(fs::is_directory(tmp.path() / "a" / "b"));
}

TEST_CASE("ensure_directory_canonical is idempotent when called twice") {
    TempDir tmp;
    std::string err;
    auto r1 = ensure_directory_canonical(tmp.path() / "store", true, &err);
    auto r2 = ensure_directory_canonical(tmp.path() / "store", true, &err);
    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());
    CHECK(*r1 == *r2);
}

TEST_CASE("ensure_directory_canonical fails when an ancestor is a regular file") {
    TempDir tmp;
    write_file(tmp.path() / "notadir", "content");
    std::string err;
    auto r = ensure_directory_canonical(tmp.path() / "notadir" / "child", true, &err);
    CHECK_FALSE(r.has_value());
}

// ---------------------------------------------------------------------------
// is_filesystem_root
// ---------------------------------------------------------------------------

TEST_CASE("is_filesystem_root is true for '/'") { CHECK(is_filesystem_root(fs::path("/"))); }

TEST_CASE("is_filesystem_root is false for a normal absolute directory") {
    CHECK_FALSE(is_filesystem_root(fs::path("/home/user")));
}

// ---------------------------------------------------------------------------
// read_symlink_target / resolve_symlink_target
// ---------------------------------------------------------------------------

TEST_CASE("read_symlink_target reads a relative symlink's raw target") {
    TempDir tmp;
    write_file(tmp.path() / "real.txt", "x");
    fs::create_symlink("real.txt", tmp.path() / "link.txt");
    auto target = read_symlink_target(tmp.path() / "link.txt");
    REQUIRE(target.has_value());
    CHECK(*target == fs::path("real.txt"));
}

TEST_CASE("read_symlink_target returns nullopt for a non-symlink path") {
    TempDir tmp;
    write_file(tmp.path() / "real.txt", "x");
    auto target = read_symlink_target(tmp.path() / "real.txt");
    CHECK_FALSE(target.has_value());
}

TEST_CASE("read_symlink_target returns nullopt for a nonexistent path") {
    TempDir tmp;
    auto target = read_symlink_target(tmp.path() / "nope");
    CHECK_FALSE(target.has_value());
}

TEST_CASE("resolve_symlink_target resolves a relative target against the link's directory") {
    fs::path link = "/home/user/.config/link";
    fs::path resolved = resolve_symlink_target(link, fs::path("../other/target"));
    CHECK(resolved.lexically_normal() == fs::path("/home/user/other/target"));
}

TEST_CASE("resolve_symlink_target preserves an absolute target unchanged") {
    fs::path link = "/home/user/.config/link";
    fs::path resolved = resolve_symlink_target(link, fs::path("/etc/absolute-target"));
    CHECK(resolved == fs::path("/etc/absolute-target"));
}
