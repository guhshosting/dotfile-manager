// ignore_rules.hpp - simple, predictable ignore-pattern matching.
//
// Supports three pattern forms, matched against a POSIX-style relative path
// (forward slashes, no leading "./"):
//   1. Exact relative path match, e.g. "config/secrets.env"
//   2. Directory-prefix match, e.g. "build/" matches "build" and anything
//      under it.
//   3. Simple glob match on the path or the final path component, supporting
//      '*' (any run of characters, not crossing '/') and '?' (single
//      character, not '/'). Patterns containing '/' are matched against the
//      whole relative path; patterns without '/' are matched against every
//      path component (so "*.swp" matches "foo/bar.swp").
//
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace dfm {

class IgnoreRules {
public:
    IgnoreRules() = default;
    explicit IgnoreRules(std::vector<std::string> patterns);

    // Returns the set of built-in default ignore patterns applied to
    // directory comparisons/backups unless the user's config overrides them.
    static std::vector<std::string> default_patterns();

    void add_pattern(const std::string& pattern);
    const std::vector<std::string>& patterns() const { return patterns_; }

    // `relative_path` must use '/' separators and contain no leading "./".
    // `is_directory` allows directory-prefix patterns ("build/") to match the
    // directory entry itself, not just its contents.
    bool is_ignored(const std::string& relative_path, bool is_directory) const;

private:
    static bool glob_match(const std::string& pattern, const std::string& text);

    std::vector<std::string> patterns_;
};

}  // namespace dfm
