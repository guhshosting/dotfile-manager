// test_diff_engine.cpp - unit tests for dfm/diff_engine.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/diff_engine.hpp"

#include "doctest.h"

using namespace dfm;

// ---------------------------------------------------------------------------
// looks_binary
// ---------------------------------------------------------------------------

TEST_CASE("looks_binary returns false for plain ASCII text") {
    CHECK_FALSE(looks_binary("hello\nworld\n"));
}

TEST_CASE("looks_binary returns false for an empty string") {
    CHECK_FALSE(looks_binary(""));
}

TEST_CASE("looks_binary returns true when content contains a NUL byte") {
    std::string s = "abc";
    s.push_back('\0');
    s += "def";
    CHECK(looks_binary(s));
}

TEST_CASE("looks_binary returns true for content dominated by control bytes") {
    std::string s;
    for (int i = 0; i < 100; ++i) s.push_back(static_cast<char>(1));  // SOH, non-printable
    CHECK(looks_binary(s));
}

TEST_CASE("looks_binary tolerates newlines, carriage returns, and tabs as text") {
    std::string s = "line1\r\n\tindented line2\r\n";
    CHECK_FALSE(looks_binary(s));
}

TEST_CASE("looks_binary tolerates UTF-8 high-bit bytes as text") {
    std::string s = "caf\xC3\xA9 na\xC3\xAFve";  // "café naïve" in UTF-8
    CHECK_FALSE(looks_binary(s));
}

TEST_CASE("looks_binary only samples the first 8000 bytes") {
    std::string s(8000, 'a');
    s.push_back('\0');  // beyond the sampled window
    s += std::string(100, 'b');
    CHECK_FALSE(looks_binary(s));
}

// ---------------------------------------------------------------------------
// split_lines
// ---------------------------------------------------------------------------

TEST_CASE("split_lines splits on newlines without a trailing empty line") {
    auto lines = split_lines("a\nb\nc\n");
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "a");
    CHECK(lines[1] == "b");
    CHECK(lines[2] == "c");
}

TEST_CASE("split_lines keeps a final line with no trailing newline") {
    auto lines = split_lines("a\nb\nc");
    REQUIRE(lines.size() == 3);
    CHECK(lines[2] == "c");
}

TEST_CASE("split_lines of an empty string returns an empty vector") {
    CHECK(split_lines("").empty());
}

TEST_CASE("split_lines of a single newline returns one empty line") {
    auto lines = split_lines("\n");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].empty());
}

TEST_CASE("split_lines handles consecutive blank lines") {
    auto lines = split_lines("a\n\n\nb\n");
    REQUIRE(lines.size() == 4);
    CHECK(lines[1].empty());
    CHECK(lines[2].empty());
}

// ---------------------------------------------------------------------------
// diff_lines
// ---------------------------------------------------------------------------

TEST_CASE("diff_lines reports identical for equal inputs") {
    std::vector<std::string> a = {"x", "y", "z"};
    auto result = diff_lines(a, a);
    CHECK(result.ok);
    CHECK(result.identical);
    CHECK(result.lines.empty());
}

TEST_CASE("diff_lines reports identical for two empty inputs") {
    auto result = diff_lines({}, {});
    CHECK(result.ok);
    CHECK(result.identical);
}

TEST_CASE("diff_lines detects a pure addition") {
    std::vector<std::string> a = {"one", "two"};
    std::vector<std::string> b = {"one", "two", "three"};
    auto result = diff_lines(a, b);
    REQUIRE(result.ok);
    CHECK_FALSE(result.identical);
    bool found_added = false;
    for (auto& l : result.lines)
        if (l.op == DiffOp::Added && l.text == "three") found_added = true;
    CHECK(found_added);
}

TEST_CASE("diff_lines detects a pure removal") {
    std::vector<std::string> a = {"one", "two", "three"};
    std::vector<std::string> b = {"one", "two"};
    auto result = diff_lines(a, b);
    REQUIRE(result.ok);
    bool found_removed = false;
    for (auto& l : result.lines)
        if (l.op == DiffOp::Removed && l.text == "three") found_removed = true;
    CHECK(found_removed);
}

TEST_CASE("diff_lines detects a mixed change (removed + added + context)") {
    std::vector<std::string> a = {"keep", "old"};
    std::vector<std::string> b = {"keep", "new"};
    auto result = diff_lines(a, b);
    REQUIRE(result.ok);
    bool has_context = false, has_removed = false, has_added = false;
    for (auto& l : result.lines) {
        if (l.op == DiffOp::Context && l.text == "keep") has_context = true;
        if (l.op == DiffOp::Removed && l.text == "old") has_removed = true;
        if (l.op == DiffOp::Added && l.text == "new") has_added = true;
    }
    CHECK(has_context);
    CHECK(has_removed);
    CHECK(has_added);
}

TEST_CASE("diff_lines against an empty first input marks all lines added") {
    std::vector<std::string> b = {"a", "b"};
    auto result = diff_lines({}, b);
    REQUIRE(result.ok);
    for (auto& l : result.lines) CHECK(l.op == DiffOp::Added);
}

TEST_CASE("diff_lines against an empty second input marks all lines removed") {
    std::vector<std::string> a = {"a", "b"};
    auto result = diff_lines(a, {});
    REQUIRE(result.ok);
    for (auto& l : result.lines) CHECK(l.op == DiffOp::Removed);
}

TEST_CASE("diff_lines refuses inputs exceeding the max_cells guard") {
    std::vector<std::string> a(1000, "a");
    std::vector<std::string> b(1000, "b");
    auto result = diff_lines(a, b, /*max_cells=*/100);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.skip_reason.empty());
}

TEST_CASE("diff_lines accepts inputs exactly at the max_cells boundary") {
    std::vector<std::string> a(10, "a");
    std::vector<std::string> b(10, "different");
    auto result = diff_lines(a, b, /*max_cells=*/100);  // 10*10 == 100, not > 100
    CHECK(result.ok);
}

// ---------------------------------------------------------------------------
// render_unified_diff
// ---------------------------------------------------------------------------

TEST_CASE("render_unified_diff reports no differences for identical input") {
    LineDiffResult diff;
    diff.ok = true;
    diff.identical = true;
    CHECK(render_unified_diff(diff, "a", "b") == "(no differences)");
}

TEST_CASE("render_unified_diff reports skip reason when diff is unavailable") {
    LineDiffResult diff;
    diff.ok = false;
    diff.skip_reason = "too large";
    auto rendered = render_unified_diff(diff, "a", "b");
    CHECK(rendered.find("too large") != std::string::npos);
}

TEST_CASE("render_unified_diff includes header labels and +/- markers") {
    auto result = diff_lines({"old"}, {"new"});
    auto rendered = render_unified_diff(result, "home:.bashrc", "store:bash/.bashrc");
    CHECK(rendered.find("--- home:.bashrc") != std::string::npos);
    CHECK(rendered.find("+++ store:bash/.bashrc") != std::string::npos);
    CHECK(rendered.find("- old") != std::string::npos);
    CHECK(rendered.find("+ new") != std::string::npos);
}
