// test_atomic_io.cpp - unit tests for dfm/atomic_io.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/atomic_io.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

using namespace dfm;
using namespace dfm_test;

TEST_CASE("atomic_write_file creates a new file with the given content") {
    TempDir tmp;
    fs::path dest = tmp.path() / "out.txt";
    auto err = atomic_write_file(dest, "hello world");
    CHECK_FALSE(err.has_value());
    CHECK(read_file(dest) == "hello world");
}

TEST_CASE("atomic_write_file creates missing parent directories") {
    TempDir tmp;
    fs::path dest = tmp.path() / "a" / "b" / "c" / "out.txt";
    auto err = atomic_write_file(dest, "nested");
    CHECK_FALSE(err.has_value());
    CHECK(fs::exists(dest));
}

TEST_CASE("atomic_write_file overwrites an existing file's content completely") {
    TempDir tmp;
    fs::path dest = tmp.path() / "out.txt";
    write_file(dest, "old content that is much longer than new");
    auto err = atomic_write_file(dest, "new");
    CHECK_FALSE(err.has_value());
    CHECK(read_file(dest) == "new");
}

TEST_CASE("atomic_write_file leaves no leftover temp files on success") {
    TempDir tmp;
    fs::path dest = tmp.path() / "out.txt";
    atomic_write_file(dest, "content");
    std::size_t count = 0;
    for (auto& entry : fs::directory_iterator(tmp.path())) {
        (void)entry;
        ++count;
    }
    CHECK(count == 1);  // only out.txt, no .out.txt.tmp-* leftovers
}

TEST_CASE("atomic_write_file applies requested permission bits") {
    TempDir tmp;
    fs::path dest = tmp.path() / "out.txt";
    auto err = atomic_write_file(dest, "x", fs::perms::owner_read | fs::perms::owner_write);
    CHECK_FALSE(err.has_value());
    auto perms = fs::status(dest).permissions();
    CHECK((perms & fs::perms::owner_read) != fs::perms::none);
    CHECK((perms & fs::perms::owner_write) != fs::perms::none);
}

TEST_CASE("atomic_write_file can write an empty string") {
    TempDir tmp;
    fs::path dest = tmp.path() / "empty.txt";
    auto err = atomic_write_file(dest, "");
    CHECK_FALSE(err.has_value());
    CHECK(read_file(dest).empty());
}

TEST_CASE("read_file_to_string returns content for a regular file") {
    TempDir tmp;
    fs::path p = tmp.path() / "f.txt";
    write_file(p, "abc123");
    std::string err;
    auto content = read_file_to_string(p, &err);
    REQUIRE(content.has_value());
    CHECK(*content == "abc123");
    CHECK(err.empty());
}

TEST_CASE("read_file_to_string fails with error message for a missing file") {
    TempDir tmp;
    std::string err;
    auto content = read_file_to_string(tmp.path() / "missing.txt", &err);
    CHECK_FALSE(content.has_value());
    CHECK_FALSE(err.empty());
}

TEST_CASE("read_file_to_string fails for a directory") {
    TempDir tmp;
    fs::create_directory(tmp.path() / "d");
    std::string err;
    auto content = read_file_to_string(tmp.path() / "d", &err);
    CHECK_FALSE(content.has_value());
    CHECK_FALSE(err.empty());
}

TEST_CASE("atomic_copy_file copies content and preserves permissions") {
    TempDir tmp;
    fs::path src = tmp.path() / "src.txt";
    write_file(src, "copy me");
    fs::permissions(src, fs::perms::owner_read | fs::perms::owner_write);
    fs::path dest = tmp.path() / "dest.txt";

    auto err = atomic_copy_file(src, dest);
    CHECK_FALSE(err.has_value());
    CHECK(read_file(dest) == "copy me");
}

TEST_CASE("atomic_copy_file fails for a missing source") {
    TempDir tmp;
    auto err = atomic_copy_file(tmp.path() / "nope.txt", tmp.path() / "dest.txt");
    CHECK(err.has_value());
}

TEST_CASE("atomic_copy_file refuses to copy a directory") {
    TempDir tmp;
    fs::create_directory(tmp.path() / "srcdir");
    auto err = atomic_copy_file(tmp.path() / "srcdir", tmp.path() / "dest");
    CHECK(err.has_value());
}

TEST_CASE("atomic_copy_file refuses to copy a symlink source directly") {
    TempDir tmp;
    write_file(tmp.path() / "target.txt", "x");
    make_symlink("target.txt", tmp.path() / "link.txt");
    auto err = atomic_copy_file(tmp.path() / "link.txt", tmp.path() / "dest.txt");
    CHECK(err.has_value());
}
