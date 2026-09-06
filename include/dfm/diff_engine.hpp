// diff_engine.hpp - conservative binary detection and line-oriented diff.
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace dfm {

// Conservative binary-file heuristic: treats content as binary if it
// contains a NUL byte within the first 8000 bytes, or if more than 30% of
// the sampled bytes are non-printable/non-whitespace control characters.
bool looks_binary(const std::string& content);

enum class DiffOp { Context, Removed, Added };

struct DiffLine {
    DiffOp op;
    std::string text;  // without trailing newline
};

struct LineDiffResult {
    bool ok = false;               // false if inputs exceeded size limits
    bool identical = false;
    std::vector<DiffLine> lines;   // valid when ok && !identical
    std::string skip_reason;       // set when !ok
};

// Splits `content` into lines (split on '\n'; a trailing newline does not
// produce a spurious empty final line, matching typical text-file
// conventions).
std::vector<std::string> split_lines(const std::string& content);

// Computes a simple LCS-based line diff between `a` and `b`. To keep the
// algorithm's O(n*m) memory bounded, refuses (ok=false) when
// a.size() * b.size() exceeds `max_cells`.
LineDiffResult diff_lines(const std::vector<std::string>& a, const std::vector<std::string>& b,
                          std::size_t max_cells = 4'000'000);

// Renders a LineDiffResult as unified-diff-style text (without file
// timestamps), given the two side labels to use in the "---"/"+++" header.
std::string render_unified_diff(const LineDiffResult& diff, const std::string& label_a,
                                 const std::string& label_b);

}  // namespace dfm
