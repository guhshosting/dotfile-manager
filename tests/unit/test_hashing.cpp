// test_hashing.cpp - unit tests for dfm/hashing.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/hashing.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

using namespace dfm;
using namespace dfm_test;

TEST_CASE("hash_bytes of empty string matches the known SHA-256 vector") {
    CHECK(hash_bytes("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("hash_bytes of 'abc' matches the known SHA-256 test vector") {
    CHECK(hash_bytes("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("hash_bytes is deterministic for the same input") {
    CHECK(hash_bytes("hello world") == hash_bytes("hello world"));
}

TEST_CASE("hash_bytes differs for different input") {
    CHECK(hash_bytes("hello") != hash_bytes("world"));
}

TEST_CASE("hash_bytes produces a 64-character lowercase hex string") {
    std::string h = hash_bytes("some content");
    CHECK(h.size() == 64);
    for (char c : h) CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
}

TEST_CASE("hash_file matches hash_bytes for identical content") {
    TempDir tmp;
    fs::path p = tmp.path() / "f.txt";
    write_file(p, "abc");
    auto h = hash_file(p);
    REQUIRE(h.has_value());
    CHECK(*h == hash_bytes("abc"));
}

TEST_CASE("hash_file handles large content across multiple internal chunks") {
    TempDir tmp;
    fs::path p = tmp.path() / "big.bin";
    std::string data(200000, 'x');  // larger than the 64KiB internal chunk size
    write_file(p, data);
    auto h = hash_file(p);
    REQUIRE(h.has_value());
    CHECK(*h == hash_bytes(data));
}

TEST_CASE("hash_file returns nullopt with error set for a missing file") {
    TempDir tmp;
    std::string err;
    auto h = hash_file(tmp.path() / "missing.txt", &err);
    CHECK_FALSE(h.has_value());
    CHECK_FALSE(err.empty());
}

TEST_CASE("hash_file returns nullopt for a directory") {
    TempDir tmp;
    fs::create_directory(tmp.path() / "d");
    std::string err;
    auto h = hash_file(tmp.path() / "d", &err);
    CHECK_FALSE(h.has_value());
    CHECK_FALSE(err.empty());
}

TEST_CASE("hash_file of an empty file matches the empty-string vector") {
    TempDir tmp;
    fs::path p = tmp.path() / "empty.txt";
    write_file(p, "");
    auto h = hash_file(p);
    REQUIRE(h.has_value());
    CHECK(*h == hash_bytes(""));
}
