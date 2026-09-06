#include "dfm/diff_engine.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace dfm {

bool looks_binary(const std::string& content) {
    const std::size_t sample_size = std::min<std::size_t>(content.size(), 8000);
    std::size_t non_text = 0;
    for (std::size_t i = 0; i < sample_size; ++i) {
        unsigned char c = static_cast<unsigned char>(content[i]);
        if (c == '\0') return true;
        bool printable_or_ws = (c >= 0x20 && c < 0x7F) || c == '\n' || c == '\r' || c == '\t' || c >= 0x80;
        if (!printable_or_ws) ++non_text;
    }
    if (sample_size == 0) return false;
    return (static_cast<double>(non_text) / static_cast<double>(sample_size)) > 0.30;
}

std::vector<std::string> split_lines(const std::string& content) {
    std::vector<std::string> lines;
    if (content.empty()) return lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            lines.emplace_back(content.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < content.size()) {
        lines.emplace_back(content.substr(start));
    }
    return lines;
}

LineDiffResult diff_lines(const std::vector<std::string>& a, const std::vector<std::string>& b,
                          std::size_t max_cells) {
    LineDiffResult result;

    if (a == b) {
        result.ok = true;
        result.identical = true;
        return result;
    }

    const std::size_t n = a.size();
    const std::size_t m = b.size();
    if (n * m > max_cells) {
        result.ok = false;
        result.skip_reason = "inputs too large for line-oriented diff (" + std::to_string(n) + "x" +
                              std::to_string(m) + " lines)";
        return result;
    }

    // Standard LCS length table, then backtrack to emit a diff.
    std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            if (a[i] == b[j]) {
                lcs[i][j] = lcs[i + 1][j + 1] + 1;
            } else {
                lcs[i][j] = std::max(lcs[i + 1][j], lcs[i][j + 1]);
            }
        }
    }

    std::vector<DiffLine> lines;
    std::size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) {
            lines.push_back({DiffOp::Context, a[i]});
            ++i;
            ++j;
        } else if (lcs[i + 1][j] >= lcs[i][j + 1]) {
            lines.push_back({DiffOp::Removed, a[i]});
            ++i;
        } else {
            lines.push_back({DiffOp::Added, b[j]});
            ++j;
        }
    }
    while (i < n) {
        lines.push_back({DiffOp::Removed, a[i]});
        ++i;
    }
    while (j < m) {
        lines.push_back({DiffOp::Added, b[j]});
        ++j;
    }

    result.ok = true;
    result.identical = false;
    result.lines = std::move(lines);
    return result;
}

std::string render_unified_diff(const LineDiffResult& diff, const std::string& label_a,
                                 const std::string& label_b) {
    if (!diff.ok) return "(diff unavailable: " + diff.skip_reason + ")";
    if (diff.identical) return "(no differences)";

    std::ostringstream out;
    out << "--- " << label_a << "\n";
    out << "+++ " << label_b << "\n";
    for (const auto& line : diff.lines) {
        switch (line.op) {
            case DiffOp::Context: out << "  " << line.text << "\n"; break;
            case DiffOp::Removed: out << "- " << line.text << "\n"; break;
            case DiffOp::Added: out << "+ " << line.text << "\n"; break;
        }
    }
    return out.str();
}

}  // namespace dfm
