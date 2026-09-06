// output.hpp - shared output formatting helpers.
// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace dfm {

enum class OutputFormat { Text, Json, Markdown };

std::optional<OutputFormat> parse_output_format(const std::string& s);
std::string to_string(OutputFormat fmt);

// A minimal table helper shared by text/markdown renderers.
struct Table {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;

    std::string render_text() const;      // simple aligned columns
    std::string render_markdown() const;  // GitHub-flavored markdown table
};

// Applies ANSI color codes only when `enabled` is true (respects --no-color).
std::string colorize(const std::string& text, const char* ansi_code, bool enabled);

}  // namespace dfm
