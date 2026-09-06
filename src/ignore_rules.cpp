#include "dfm/ignore_rules.hpp"

#include <algorithm>
#include <sstream>

namespace dfm {

IgnoreRules::IgnoreRules(std::vector<std::string> patterns) : patterns_(std::move(patterns)) {}

std::vector<std::string> IgnoreRules::default_patterns() {
    return {".git", ".cache", "node_modules", "__pycache__", "*.swp", "*.tmp", "*.log"};
}

void IgnoreRules::add_pattern(const std::string& pattern) {
    if (!pattern.empty()) patterns_.push_back(pattern);
}

namespace {
std::vector<std::string> split_components(const std::string& path) {
    std::vector<std::string> out;
    std::stringstream ss(path);
    std::string item;
    while (std::getline(ss, item, '/')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}
}  // namespace

bool IgnoreRules::glob_match(const std::string& pattern, const std::string& text) {
    // Simple '*'/'?' glob matcher, '*' does not cross the caller's chosen
    // matching unit (we call this per-component or on the full joined path,
    // so '*' naturally cannot cross '/' when matching components; when
    // matching a pattern containing '/' against the full path, '*' is
    // allowed to match any characters including '/', matching common glob
    // tool behavior for simple non-recursive globs used here).
    std::size_t pi = 0, ti = 0;
    std::size_t star_p = std::string::npos, star_t = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            ++pi;
            ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star_p = pi++;
            star_t = ti;
        } else if (star_p != std::string::npos) {
            pi = star_p + 1;
            ti = ++star_t;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

bool IgnoreRules::is_ignored(const std::string& relative_path, bool is_directory) const {
    if (relative_path.empty()) return false;
    auto components = split_components(relative_path);
    if (components.empty()) return false;
    const std::string& basename = components.back();

    for (const auto& raw_pattern : patterns_) {
        if (raw_pattern.empty()) continue;

        // Directory-prefix pattern: "name/" matches that directory itself
        // (when is_directory) and everything nested under it.
        if (raw_pattern.back() == '/') {
            std::string dirname = raw_pattern.substr(0, raw_pattern.size() - 1);
            if (dirname.empty()) continue;
            bool matches_prefix = false;
            for (const auto& comp : components) {
                if (comp == dirname) {
                    matches_prefix = true;
                    break;
                }
                // Only treat as prefix match if directory component precedes
                // remaining path; check progressively.
            }
            // More precise prefix check: any leading sequence of components
            // ending in dirname.
            std::string built;
            for (std::size_t i = 0; i < components.size(); ++i) {
                built = i == 0 ? components[i] : built + "/" + components[i];
                if (components[i] == dirname) {
                    matches_prefix = true;
                    break;
                }
            }
            if (matches_prefix) return true;
            continue;
        }

        // Exact relative path match.
        if (raw_pattern == relative_path) return true;
        if (is_directory && raw_pattern == relative_path + "/") return true;

        bool has_slash = raw_pattern.find('/') != std::string::npos;
        bool has_wildcard = raw_pattern.find('*') != std::string::npos ||
                             raw_pattern.find('?') != std::string::npos;

        if (has_slash) {
            if (has_wildcard) {
                if (glob_match(raw_pattern, relative_path)) return true;
            }
            continue;
        }

        if (has_wildcard) {
            // No slash: match against every path component (so "*.swp"
            // matches at any depth), and also against the full relative
            // path for single-component paths.
            for (const auto& comp : components) {
                if (glob_match(raw_pattern, comp)) return true;
            }
        } else {
            // Bare name with no slash and no wildcard: matches any path
            // component exactly (e.g. ".git" ignores ".git" anywhere).
            if (basename == raw_pattern) return true;
            for (const auto& comp : components) {
                if (comp == raw_pattern) return true;
            }
        }
    }
    return false;
}

}  // namespace dfm
