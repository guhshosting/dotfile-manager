#include "dfm/compare.hpp"

#include "dfm/hashing.hpp"
#include "dfm/path_safety.hpp"

#include <deque>

namespace dfm {

namespace fs = std::filesystem;

std::string to_string(EntryStatus status) {
    switch (status) {
        case EntryStatus::InSync: return "IN_SYNC";
        case EntryStatus::Modified: return "MODIFIED";
        case EntryStatus::MissingHome: return "MISSING_HOME";
        case EntryStatus::MissingStore: return "MISSING_STORE";
        case EntryStatus::TypeMismatch: return "TYPE_MISMATCH";
        case EntryStatus::SymlinkMismatch: return "SYMLINK_MISMATCH";
        case EntryStatus::Conflict: return "CONFLICT";
        case EntryStatus::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

std::string to_string(DirChangeType change) {
    switch (change) {
        case DirChangeType::Added: return "added";
        case DirChangeType::Removed: return "removed";
        case DirChangeType::Modified: return "modified";
        case DirChangeType::TypeChanged: return "type_changed";
        case DirChangeType::SymlinkTargetChanged: return "symlink_target_changed";
    }
    return "unknown";
}

namespace {

std::string join_rel(const std::string& prefix, const std::string& name) {
    return prefix.empty() ? name : prefix + "/" + name;
}

void list_tree_recursive(const fs::path& base_root, const fs::path& dir, const std::string& rel_prefix,
                          const IgnoreRules& ignore, std::map<std::string, TreeNode>& out) {
    std::error_code ec;
    std::vector<fs::directory_entry> children;
    for (const auto& child : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        children.push_back(child);
    }
    if (ec) return;  // unreadable directory: report nothing further for it

    for (const auto& child : children) {
        std::string name = child.path().filename().string();
        std::string rel = join_rel(rel_prefix, name);

        auto info = inspect(child.path());
        bool is_dir_like = (info.kind == EntryKind::Directory);
        if (ignore.is_ignored(rel, is_dir_like)) continue;

        TreeNode node;
        node.kind = info.kind;
        switch (info.kind) {
            case EntryKind::Regular: {
                std::string herr;
                auto h = hash_file(child.path(), &herr);
                node.hash = h.value_or(std::string());
                break;
            }
            case EntryKind::Symlink: {
                auto target = read_symlink_target(child.path());
                if (target) node.symlink_target = *target;
                break;
            }
            case EntryKind::Directory:
                out[rel] = node;
                list_tree_recursive(base_root, child.path(), rel, ignore, out);
                continue;
            default:
                break;  // special files: recorded by kind only, never read
        }
        out[rel] = node;
    }
}

}  // namespace

std::map<std::string, TreeNode> list_tree(const fs::path& root, const IgnoreRules& ignore) {
    std::map<std::string, TreeNode> out;
    if (!is_directory_no_follow(root)) return out;
    list_tree_recursive(root, root, "", ignore, out);
    return out;
}

std::vector<DirDiffItem> diff_trees(const std::map<std::string, TreeNode>& store_tree,
                                     const std::map<std::string, TreeNode>& home_tree) {
    std::vector<DirDiffItem> diffs;

    for (const auto& [rel, store_node] : store_tree) {
        auto it = home_tree.find(rel);
        if (it == home_tree.end()) {
            diffs.push_back({rel, DirChangeType::Removed});  // present in store, missing in home
            continue;
        }
        const TreeNode& home_node = it->second;
        if (store_node.kind != home_node.kind) {
            diffs.push_back({rel, DirChangeType::TypeChanged});
            continue;
        }
        switch (store_node.kind) {
            case EntryKind::Regular:
                if (store_node.hash != home_node.hash) diffs.push_back({rel, DirChangeType::Modified});
                break;
            case EntryKind::Symlink:
                if (store_node.symlink_target != home_node.symlink_target) {
                    diffs.push_back({rel, DirChangeType::SymlinkTargetChanged});
                }
                break;
            default:
                break;  // directories: presence/type already checked; other special files ignored
        }
    }

    for (const auto& [rel, home_node] : home_tree) {
        (void)home_node;
        if (store_tree.find(rel) == store_tree.end()) {
            diffs.push_back({rel, DirChangeType::Added});  // present in home, absent from store
        }
    }

    return diffs;
}

EntryStatusResult compute_status(const fs::path& home_dir, const fs::path& store_dir, const ManagedEntry& entry,
                                  const IgnoreRules& ignore) {
    EntryStatusResult result;

    auto home_resolved = resolve_within(home_dir, entry.home_path);
    if (!home_resolved.ok()) {
        result.status = EntryStatus::Conflict;
        result.detail = "home path unsafe: " + home_resolved.message;
        return result;
    }
    auto store_resolved = resolve_within(store_dir, entry.store_path);
    if (!store_resolved.ok()) {
        result.status = EntryStatus::Conflict;
        result.detail = "store path unsafe: " + store_resolved.message;
        return result;
    }

    result.home_abs = home_resolved.resolved;
    result.store_abs = store_resolved.resolved;

    auto home_info = inspect(result.home_abs);
    auto store_info = inspect(result.store_abs);

    if (store_info.kind == EntryKind::NotFound) {
        result.status = EntryStatus::MissingStore;
        result.detail = "no copy exists in the managed store";
        return result;
    }
    if (store_info.kind == EntryKind::Inaccessible || store_info.kind == EntryKind::Unknown) {
        result.status = EntryStatus::Unknown;
        result.detail = "store path could not be inspected";
        return result;
    }
    if (store_info.kind == EntryKind::Fifo || store_info.kind == EntryKind::Socket ||
        store_info.kind == EntryKind::BlockDevice || store_info.kind == EntryKind::CharDevice) {
        result.status = EntryStatus::Conflict;
        result.detail = "store path is a special file (" + dfm::to_string(store_info.kind) + "); refusing to manage";
        return result;
    }

    if (home_info.kind == EntryKind::NotFound) {
        result.status = EntryStatus::MissingHome;
        result.detail = "not present at the home destination";
        return result;
    }
    if (home_info.kind == EntryKind::Inaccessible || home_info.kind == EntryKind::Unknown) {
        result.status = EntryStatus::Unknown;
        result.detail = "home path could not be inspected";
        return result;
    }
    if (home_info.kind == EntryKind::Fifo || home_info.kind == EntryKind::Socket ||
        home_info.kind == EntryKind::BlockDevice || home_info.kind == EntryKind::CharDevice) {
        result.status = EntryStatus::Conflict;
        result.detail = "home path is a special file (" + dfm::to_string(home_info.kind) + "); refusing to manage";
        return result;
    }

    if (entry.mode == ManageMode::Symlink) {
        if (home_info.kind != EntryKind::Symlink) {
            result.status = EntryStatus::SymlinkMismatch;
            result.detail = "entry is symlink-managed but home path is real content, not a symlink";
            return result;
        }
        auto target = read_symlink_target(result.home_abs);
        auto resolved_target = target ? resolve_symlink_target(result.home_abs, *target) : fs::path();
        auto canon_store = try_canonical(result.store_abs);
        auto canon_target = try_canonical(result.home_abs);  // follows the link fully
        bool matches = target.has_value() && canon_store.has_value() && canon_target.has_value() &&
                       *canon_store == *canon_target;
        if (!matches) {
            result.status = EntryStatus::SymlinkMismatch;
            result.detail = "home symlink does not point at the managed store copy";
            return result;
        }
        result.status = EntryStatus::InSync;
        result.detail = "symlink correctly points at the managed store";
        return result;
    }

    // Copy mode from here on.
    if (home_info.kind == EntryKind::Symlink) {
        result.status = EntryStatus::SymlinkMismatch;
        result.detail = "entry is copy-managed but home path is unexpectedly a symlink";
        return result;
    }
    if (store_info.kind != home_info.kind) {
        result.status = EntryStatus::TypeMismatch;
        result.detail =
            "type differs: store is " + dfm::to_string(store_info.kind) + ", home is " + dfm::to_string(home_info.kind);
        return result;
    }

    if (store_info.kind == EntryKind::Regular) {
        std::string err_a, err_b;
        auto h_store = hash_file(result.store_abs, &err_a);
        auto h_home = hash_file(result.home_abs, &err_b);
        if (!h_store || !h_home) {
            result.status = EntryStatus::Unknown;
            result.detail = "failed to hash file contents: " + (err_a.empty() ? err_b : err_a);
            return result;
        }
        if (*h_store == *h_home) {
            result.status = EntryStatus::InSync;
            result.detail = "content matches";
        } else {
            result.status = EntryStatus::Modified;
            result.detail = "content differs from the managed store copy";
        }
        return result;
    }

    if (store_info.kind == EntryKind::Directory) {
        result.is_directory_entry = true;
        IgnoreRules combined = ignore;
        for (const auto& p : entry.ignore) combined.add_pattern(p);

        auto store_tree = list_tree(result.store_abs, combined);
        auto home_tree = list_tree(result.home_abs, combined);
        auto diffs = diff_trees(store_tree, home_tree);
        if (diffs.empty()) {
            result.status = EntryStatus::InSync;
            result.detail = "directory contents match";
        } else {
            result.status = EntryStatus::Modified;
            result.detail = std::to_string(diffs.size()) + " differing entr" + (diffs.size() == 1 ? "y" : "ies") +
                             " under the directory";
            result.directory_changes = std::move(diffs);
        }
        return result;
    }

    result.status = EntryStatus::Unknown;
    result.detail = "unsupported entry kind";
    return result;
}

}  // namespace dfm
