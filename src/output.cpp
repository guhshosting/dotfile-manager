#include "dfm/output.hpp"

#include <algorithm>

namespace dfm {

std::optional<OutputFormat> parse_output_format(const std::string& s) {
    if (s == "text") return OutputFormat::Text;
    if (s == "json") return OutputFormat::Json;
    if (s == "markdown") return OutputFormat::Markdown;
    return std::nullopt;
}

std::string to_string(OutputFormat fmt) {
    switch (fmt) {
        case OutputFormat::Text: return "text";
        case OutputFormat::Json: return "json";
        case OutputFormat::Markdown: return "markdown";
    }
    return "text";
}

std::string colorize(const std::string& text, const char* ansi_code, bool enabled) {
    if (!enabled) return text;
    return std::string("\033[") + ansi_code + "m" + text + "\033[0m";
}

std::string Table::render_text() const {
    std::vector<std::size_t> widths(headers.size(), 0);
    for (std::size_t i = 0; i < headers.size(); ++i) widths[i] = headers[i].size();
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size() && i < widths.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }
    std::ostringstream out;
    auto emit_row = [&](const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            out << cells[i];
            if (i + 1 < cells.size()) out << std::string(widths[i] - cells[i].size() + 2, ' ');
        }
        out << "\n";
    };
    emit_row(headers);
    for (std::size_t i = 0; i < headers.size(); ++i) {
        out << std::string(widths[i], '-');
        if (i + 1 < headers.size()) out << "  ";
    }
    out << "\n";
    for (const auto& row : rows) emit_row(row);
    return out.str();
}

std::string Table::render_markdown() const {
    std::ostringstream out;
    out << "|";
    for (const auto& h : headers) out << " " << h << " |";
    out << "\n|";
    for (std::size_t i = 0; i < headers.size(); ++i) out << " --- |";
    out << "\n";
    for (const auto& row : rows) {
        out << "|";
        for (const auto& cell : row) out << " " << cell << " |";
        out << "\n";
    }
    return out.str();
}

}  // namespace dfm
