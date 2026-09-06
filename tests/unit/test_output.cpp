// test_output.cpp - unit tests for dfm/output.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/output.hpp"

#include "doctest.h"

using namespace dfm;

TEST_CASE("parse_output_format parses text, json, and markdown") {
    CHECK(parse_output_format("text") == OutputFormat::Text);
    CHECK(parse_output_format("json") == OutputFormat::Json);
    CHECK(parse_output_format("markdown") == OutputFormat::Markdown);
}

TEST_CASE("parse_output_format rejects unknown format strings") {
    CHECK_FALSE(parse_output_format("yaml").has_value());
    CHECK_FALSE(parse_output_format("").has_value());
    CHECK_FALSE(parse_output_format("JSON").has_value());  // case-sensitive
}

TEST_CASE("to_string(OutputFormat) round-trips through parse_output_format") {
    CHECK(parse_output_format(to_string(OutputFormat::Text)) == OutputFormat::Text);
    CHECK(parse_output_format(to_string(OutputFormat::Json)) == OutputFormat::Json);
    CHECK(parse_output_format(to_string(OutputFormat::Markdown)) == OutputFormat::Markdown);
}

TEST_CASE("colorize returns plain text when disabled") {
    CHECK(colorize("hello", "31", false) == "hello");
}

TEST_CASE("colorize wraps text in ANSI codes when enabled") {
    auto result = colorize("hello", "31", true);
    CHECK(result.find("hello") != std::string::npos);
    CHECK(result.find("\033[31m") != std::string::npos);
    CHECK(result.find("\033[0m") != std::string::npos);
}

TEST_CASE("Table::render_text produces aligned columns with a header and separator") {
    Table t;
    t.headers = {"NAME", "STATUS"};
    t.rows = {{".bashrc", "IN_SYNC"}, {".vimrc", "MODIFIED"}};
    auto text = t.render_text();
    CHECK(text.find("NAME") != std::string::npos);
    CHECK(text.find("STATUS") != std::string::npos);
    CHECK(text.find(".bashrc") != std::string::npos);
    CHECK(text.find("MODIFIED") != std::string::npos);
    CHECK(text.find("----") != std::string::npos);
}

TEST_CASE("Table::render_text with no rows still renders the header") {
    Table t;
    t.headers = {"A", "B"};
    auto text = t.render_text();
    CHECK(text.find("A") != std::string::npos);
    CHECK(text.find("B") != std::string::npos);
}

TEST_CASE("Table::render_markdown produces a well-formed GitHub table") {
    Table t;
    t.headers = {"NAME", "STATUS"};
    t.rows = {{".bashrc", "IN_SYNC"}};
    auto md = t.render_markdown();
    CHECK(md.find("| NAME | STATUS |") != std::string::npos);
    CHECK(md.find("--- ") != std::string::npos);
    CHECK(md.find("| .bashrc | IN_SYNC |") != std::string::npos);
}

TEST_CASE("Table::render_markdown handles an empty table gracefully") {
    Table t;
    t.headers = {"X"};
    auto md = t.render_markdown();
    CHECK(md.find("| X |") != std::string::npos);
}
