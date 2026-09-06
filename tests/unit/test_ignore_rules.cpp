// test_ignore_rules.cpp - unit tests for dfm/ignore_rules.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/ignore_rules.hpp"

#include "doctest.h"

#include <algorithm>

using namespace dfm;

TEST_CASE("default_patterns includes the documented minimum set") {
    auto pats = IgnoreRules::default_patterns();
    for (const char* expected : {".git", ".cache", "node_modules", "__pycache__", "*.swp", "*.tmp", "*.log"}) {
        CHECK(std::find(pats.begin(), pats.end(), expected) != pats.end());
    }
}

TEST_CASE("IgnoreRules with default patterns ignores .git anywhere") {
    IgnoreRules rules(IgnoreRules::default_patterns());
    CHECK(rules.is_ignored(".git", true));
    CHECK(rules.is_ignored("project/.git", true));
}

TEST_CASE("IgnoreRules exact relative path match") {
    IgnoreRules rules({"config/secrets.env"});
    CHECK(rules.is_ignored("config/secrets.env", false));
    CHECK_FALSE(rules.is_ignored("config/other.env", false));
}

TEST_CASE("IgnoreRules directory-prefix match ignores the directory itself") {
    IgnoreRules rules({"build/"});
    CHECK(rules.is_ignored("build", true));
}

TEST_CASE("IgnoreRules directory-prefix match ignores nested content") {
    IgnoreRules rules({"build/"});
    CHECK(rules.is_ignored("build/output/bin.o", false));
}

TEST_CASE("IgnoreRules directory-prefix match does not ignore an unrelated sibling") {
    IgnoreRules rules({"build/"});
    CHECK_FALSE(rules.is_ignored("buildscripts/run.sh", false));
}

TEST_CASE("IgnoreRules glob '*' matches suffix at any depth") {
    IgnoreRules rules({"*.swp"});
    CHECK(rules.is_ignored("foo.swp", false));
    CHECK(rules.is_ignored("dir/sub/foo.swp", false));
    CHECK_FALSE(rules.is_ignored("foo.swp.bak", false));
}

TEST_CASE("IgnoreRules glob '?' matches exactly one character") {
    IgnoreRules rules({"file?.tmp"});
    CHECK(rules.is_ignored("file1.tmp", false));
    CHECK_FALSE(rules.is_ignored("file12.tmp", false));
}

TEST_CASE("IgnoreRules bare name without wildcard matches any path component") {
    IgnoreRules rules({"__pycache__"});
    CHECK(rules.is_ignored("__pycache__", true));
    CHECK(rules.is_ignored("src/__pycache__", true));
    CHECK(rules.is_ignored("src/__pycache__/mod.pyc", false));
}

TEST_CASE("IgnoreRules pattern with slash and wildcard matches full relative path") {
    IgnoreRules rules({"logs/*.log"});
    CHECK(rules.is_ignored("logs/today.log", false));
    CHECK_FALSE(rules.is_ignored("other/today.log", false));
}

TEST_CASE("IgnoreRules does not ignore ordinary managed configuration files by default") {
    IgnoreRules rules(IgnoreRules::default_patterns());
    CHECK_FALSE(rules.is_ignored(".bashrc", false));
    CHECK_FALSE(rules.is_ignored(".gitconfig", false));
    CHECK_FALSE(rules.is_ignored("nvim/init.vim", false));
}

TEST_CASE("IgnoreRules is_ignored returns false for an empty relative path") {
    IgnoreRules rules(IgnoreRules::default_patterns());
    CHECK_FALSE(rules.is_ignored("", false));
}

TEST_CASE("IgnoreRules add_pattern appends and ignores empty strings") {
    IgnoreRules rules;
    rules.add_pattern("*.bak");
    rules.add_pattern("");
    CHECK(rules.patterns().size() == 1);
    CHECK(rules.is_ignored("thing.bak", false));
}

TEST_CASE("IgnoreRules constructed with an empty pattern list ignores nothing") {
    IgnoreRules rules;
    CHECK_FALSE(rules.is_ignored(".bashrc", false));
    CHECK_FALSE(rules.is_ignored("anything/at/all.txt", false));
}
