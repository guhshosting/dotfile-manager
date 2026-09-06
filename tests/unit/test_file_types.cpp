// test_file_types.cpp - unit tests for dfm/file_types.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/file_types.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

#include <sys/stat.h>
#include <sys/types.h>

using namespace dfm;
using namespace dfm_test;

TEST_CASE("inspect reports NotFound for a nonexistent path") {
    TempDir tmp;
    auto info = inspect(tmp.path() / "nope");
    CHECK(info.kind == EntryKind::NotFound);
}

TEST_CASE("inspect reports Regular for a plain file") {
    TempDir tmp;
    write_file(tmp.path() / "f.txt", "hello");
    auto info = inspect(tmp.path() / "f.txt");
    CHECK(info.kind == EntryKind::Regular);
    CHECK(info.size == 5);
    CHECK_FALSE(info.is_symlink);
}

TEST_CASE("inspect reports Directory for a directory") {
    TempDir tmp;
    fs::create_directory(tmp.path() / "d");
    auto info = inspect(tmp.path() / "d");
    CHECK(info.kind == EntryKind::Directory);
}

TEST_CASE("inspect reports Symlink without following it") {
    TempDir tmp;
    write_file(tmp.path() / "target.txt", "x");
    fs::create_symlink("target.txt", tmp.path() / "link.txt");
    auto info = inspect(tmp.path() / "link.txt");
    CHECK(info.kind == EntryKind::Symlink);
    CHECK(info.is_symlink);
}

TEST_CASE("inspect reports Symlink for a dangling symlink rather than NotFound") {
    TempDir tmp;
    fs::create_symlink("does-not-exist", tmp.path() / "dangling.txt");
    auto info = inspect(tmp.path() / "dangling.txt");
    CHECK(info.kind == EntryKind::Symlink);
}

TEST_CASE("inspect reports Fifo for a named pipe") {
    TempDir tmp;
    fs::path fifo = tmp.path() / "myfifo";
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
    auto info = inspect(fifo);
    CHECK(info.kind == EntryKind::Fifo);
}

TEST_CASE("is_supported_kind accepts Regular, Directory, Symlink") {
    CHECK(is_supported_kind(EntryKind::Regular));
    CHECK(is_supported_kind(EntryKind::Directory));
    CHECK(is_supported_kind(EntryKind::Symlink));
}

TEST_CASE("is_supported_kind rejects special files") {
    CHECK_FALSE(is_supported_kind(EntryKind::Fifo));
    CHECK_FALSE(is_supported_kind(EntryKind::Socket));
    CHECK_FALSE(is_supported_kind(EntryKind::BlockDevice));
    CHECK_FALSE(is_supported_kind(EntryKind::CharDevice));
}

TEST_CASE("is_supported_kind rejects NotFound and Unknown/Inaccessible") {
    CHECK_FALSE(is_supported_kind(EntryKind::NotFound));
    CHECK_FALSE(is_supported_kind(EntryKind::Unknown));
    CHECK_FALSE(is_supported_kind(EntryKind::Inaccessible));
}

TEST_CASE("exists_no_follow / is_regular_no_follow / is_directory_no_follow / is_symlink_no_follow agree with inspect") {
    TempDir tmp;
    write_file(tmp.path() / "f.txt", "x");
    fs::create_directory(tmp.path() / "d");
    fs::create_symlink("f.txt", tmp.path() / "l");

    CHECK(exists_no_follow(tmp.path() / "f.txt"));
    CHECK(is_regular_no_follow(tmp.path() / "f.txt"));
    CHECK_FALSE(is_directory_no_follow(tmp.path() / "f.txt"));
    CHECK_FALSE(is_symlink_no_follow(tmp.path() / "f.txt"));

    CHECK(is_directory_no_follow(tmp.path() / "d"));
    CHECK_FALSE(is_regular_no_follow(tmp.path() / "d"));

    CHECK(is_symlink_no_follow(tmp.path() / "l"));
    CHECK_FALSE(is_regular_no_follow(tmp.path() / "l"));
    CHECK_FALSE(exists_no_follow(tmp.path() / "nonexistent"));
}

TEST_CASE("to_string(EntryKind) never returns an empty string") {
    for (auto k : {EntryKind::NotFound, EntryKind::Regular, EntryKind::Directory, EntryKind::Symlink,
                    EntryKind::Fifo, EntryKind::Socket, EntryKind::BlockDevice, EntryKind::CharDevice,
                    EntryKind::Unknown, EntryKind::Inaccessible}) {
        CHECK_FALSE(to_string(k).empty());
    }
}
