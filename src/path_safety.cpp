#include "dfm/path_safety.hpp"

#include <system_error>
#include <vector>

namespace dfm {

namespace fs = std::filesystem;

std::string to_string(PathSafetyError err) {
    switch (err) {
        case PathSafetyError::None: return "no error";
        case PathSafetyError::Empty: return "path is empty";
        case PathSafetyError::Absolute: return "absolute paths are not permitted here";
        case PathSafetyError::ParentTraversal: return "path contains a '..' component";
        case PathSafetyError::NullByte: return "path contains an embedded NUL byte";
        case PathSafetyError::EmptyComponent: return "path contains an empty component";
        case PathSafetyError::EscapesBase: return "path escapes the required base directory";
        case PathSafetyError::SymlinkEscapesBase:
            return "a symlink component resolves outside the required base directory";
        case PathSafetyError::FilesystemLoop: return "filesystem loop (circular symlink) detected";
        case PathSafetyError::ParentNotDirectory:
            return "an intermediate path component exists but is not a directory";
        case PathSafetyError::BrokenIntermediateSymlink:
            return "an intermediate path component is a broken (dangling) symlink";
        case PathSafetyError::CanonicalizeFailed: return "failed to canonicalize path";
        case PathSafetyError::IsRoot: return "refusing to operate on filesystem root '/'";
    }
    return "unknown path safety error";
}

std::optional<fs::path> try_canonical(const fs::path& p) {
    std::error_code ec;
    fs::path result = fs::canonical(p, ec);
    if (ec) return std::nullopt;
    return result;
}

std::optional<fs::path> try_weakly_canonical(const fs::path& p) {
    std::error_code ec;
    fs::path result = fs::weakly_canonical(p, ec);
    if (ec) return std::nullopt;
    return result;
}

bool is_filesystem_root(const fs::path& p) {
    auto canon = try_canonical(p);
    if (!canon) {
        // Fall back to lexical comparison if it doesn't exist yet.
        return p == p.root_path();
    }
    return *canon == canon->root_path();
}

bool is_within(const fs::path& base, const fs::path& candidate) {
    auto base_it = base.begin();
    auto cand_it = candidate.begin();
    for (; base_it != base.end(); ++base_it, ++cand_it) {
        if (cand_it == candidate.end()) return false;
        if (*base_it != *cand_it) return false;
    }
    return true;  // candidate has at least all components of base, in order
}

PathSafetyResult validate_relative_path(const std::string& rel) {
    PathSafetyResult r;
    if (rel.empty()) {
        r.error = PathSafetyError::Empty;
        r.message = to_string(r.error);
        return r;
    }
    if (rel.find('\0') != std::string::npos) {
        r.error = PathSafetyError::NullByte;
        r.message = to_string(r.error);
        return r;
    }

    fs::path p(rel);
    if (p.is_absolute()) {
        r.error = PathSafetyError::Absolute;
        r.message = to_string(r.error);
        return r;
    }

    fs::path normalized;
    for (const auto& comp : p) {
        const std::string c = comp.string();
        if (c.empty()) {
            r.error = PathSafetyError::EmptyComponent;
            r.message = to_string(r.error);
            return r;
        }
        if (c == ".") {
            continue;  // drop redundant current-dir components
        }
        if (c == "..") {
            r.error = PathSafetyError::ParentTraversal;
            r.message = to_string(r.error);
            return r;
        }
        normalized /= comp;
    }

    if (normalized.empty()) {
        // Path was made up entirely of "." components, e.g. "./" or ".".
        r.error = PathSafetyError::Empty;
        r.message = "path resolves to an empty relative path";
        return r;
    }

    r.error = PathSafetyError::None;
    r.resolved = normalized;
    return r;
}

std::optional<fs::path> read_symlink_target(const fs::path& link_path) {
    std::error_code ec;
    auto st = fs::symlink_status(link_path, ec);
    if (ec || !fs::is_symlink(st)) return std::nullopt;
    fs::path target = fs::read_symlink(link_path, ec);
    if (ec) return std::nullopt;
    return target;
}

fs::path resolve_symlink_target(const fs::path& link_path, const fs::path& raw_target) {
    if (raw_target.is_absolute()) return raw_target.lexically_normal();
    return (link_path.parent_path() / raw_target).lexically_normal();
}

PathSafetyResult resolve_within(const fs::path& base_dir, const std::string& relative) {
    PathSafetyResult r;

    auto base_canon = try_canonical(base_dir);
    if (!base_canon) {
        r.error = PathSafetyError::CanonicalizeFailed;
        r.message = "base directory does not exist or could not be canonicalized: " + base_dir.string();
        return r;
    }
    if (is_filesystem_root(*base_canon)) {
        r.error = PathSafetyError::IsRoot;
        r.message = to_string(r.error);
        return r;
    }

    auto rel_check = validate_relative_path(relative);
    if (!rel_check.ok()) return rel_check;

    const fs::path& rel_path = rel_check.resolved;

    // Split into components so we can walk them one at a time.
    std::vector<fs::path> components;
    for (const auto& c : rel_path) components.push_back(c);

    fs::path current = *base_canon;
    for (std::size_t i = 0; i < components.size(); ++i) {
        const bool is_leaf = (i + 1 == components.size());
        current /= components[i];

        std::error_code ec;
        auto st = fs::symlink_status(current, ec);
        if (ec) {
            // Component does not exist (or is unreachable) - nothing further
            // to validate against symlink escape for this or deeper
            // components, since there is nothing on disk to redirect us.
            continue;
        }

        if (fs::is_symlink(st)) {
            if (is_leaf) {
                // Leaf symlinks are intentionally NOT dereferenced here.
                // Higher-level command logic decides how to treat an
                // existing symlink destination (refuse / replace / etc).
                continue;
            }
            // Intermediate symlink: must resolve within base, and must not
            // be part of a cycle.
            auto canon = try_canonical(current);
            if (!canon) {
                // Could be a loop or a dangling target; distinguish using
                // errno via a second, explicit stat of the raw target chain
                // length. std::filesystem does not expose errno cleanly, so
                // we conservatively report a loop only when repeated
                // canonicalization of the *link itself* keeps failing with
                // too_many_symbolic_links; otherwise treat as broken link.
                std::error_code ec2;
                fs::path loop_probe = fs::canonical(current, ec2);
                (void)loop_probe;
                if (ec2 == std::errc::too_many_symbolic_link_levels) {
                    r.error = PathSafetyError::FilesystemLoop;
                } else {
                    r.error = PathSafetyError::BrokenIntermediateSymlink;
                }
                r.message = to_string(r.error) + ": " + current.string();
                return r;
            }
            if (!is_within(*base_canon, *canon)) {
                r.error = PathSafetyError::SymlinkEscapesBase;
                r.message = "intermediate symlink '" + current.string() +
                             "' resolves outside base directory '" + base_canon->string() + "'";
                return r;
            }
            current = *canon;
        } else if (!is_leaf && !fs::is_directory(st)) {
            r.error = PathSafetyError::ParentNotDirectory;
            r.message = "path component is not a directory: " + current.string();
            return r;
        }
    }

    r.error = PathSafetyError::None;
    r.resolved = current;
    return r;
}

std::optional<fs::path> ensure_directory_canonical(const fs::path& dir, bool create_if_missing,
                                                    std::string* error_message) {
    auto set_err = [&](const std::string& m) {
        if (error_message) *error_message = m;
    };

    if (dir.empty()) {
        set_err("directory path is empty");
        return std::nullopt;
    }
    if (!dir.is_absolute()) {
        set_err("directory path must be absolute: " + dir.string());
        return std::nullopt;
    }

    auto existing = try_canonical(dir);
    if (existing) {
        std::error_code ec;
        if (!fs::is_directory(*existing, ec)) {
            set_err("path exists but is not a directory: " + dir.string());
            return std::nullopt;
        }
        return existing;
    }

    if (!create_if_missing) {
        set_err("directory does not exist: " + dir.string());
        return std::nullopt;
    }

    // Find the deepest existing ancestor and confirm it is a real directory.
    fs::path ancestor = dir.parent_path();
    std::vector<fs::path> missing_components;
    while (!ancestor.empty()) {
        auto canon = try_canonical(ancestor);
        if (canon) {
            std::error_code ec;
            if (!fs::is_directory(*canon, ec)) {
                set_err("ancestor path exists but is not a directory: " + ancestor.string());
                return std::nullopt;
            }
            break;
        }
        missing_components.push_back(ancestor.filename());
        ancestor = ancestor.parent_path();
    }

    auto base_canon = try_canonical(ancestor);
    if (!base_canon) {
        set_err("no existing ancestor directory found for: " + dir.string());
        return std::nullopt;
    }

    // Recreate the missing path underneath the verified ancestor, one
    // component at a time, so we never silently traverse a symlink placed by
    // a third party in between validation and creation of each directory.
    fs::path build = *base_canon;
    // missing_components were pushed from deepest-parent-first while walking
    // upward, so reverse to get creation order.
    std::vector<fs::path> creation_order(missing_components.rbegin(), missing_components.rend());
    // Add the final leaf directory name too (dir's own filename).
    creation_order.push_back(dir.filename());

    for (const auto& comp : creation_order) {
        build /= comp;
        std::error_code ec;
        auto st = fs::symlink_status(build, ec);
        if (!ec && fs::is_symlink(st)) {
            set_err("refusing to create directory through existing symlink: " + build.string());
            return std::nullopt;
        }
        if (ec) {
            fs::create_directory(build, ec);
            if (ec) {
                set_err("failed to create directory '" + build.string() + "': " + ec.message());
                return std::nullopt;
            }
        } else if (!fs::is_directory(build, ec)) {
            set_err("path component exists but is not a directory: " + build.string());
            return std::nullopt;
        }
    }

    auto final_canon = try_canonical(dir);
    if (!final_canon) {
        set_err("failed to canonicalize directory after creation: " + dir.string());
        return std::nullopt;
    }
    return final_canon;
}

}  // namespace dfm
