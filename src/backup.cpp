#include "dfm/backup.hpp"

#include "dfm/atomic_io.hpp"
#include "dfm/compare.hpp"
#include "dfm/hashing.hpp"
#include "dfm/path_safety.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace dfm {

namespace fs = std::filesystem;

std::string to_string(SyncOutcome outcome) {
    switch (outcome) {
        case SyncOutcome::NoChangeInSync: return "no_change";
        case SyncOutcome::WouldCreate: return "would_create";
        case SyncOutcome::Created: return "created";
        case SyncOutcome::WouldUpdate: return "would_update";
        case SyncOutcome::Updated: return "updated";
        case SyncOutcome::WouldReplaceSymlink: return "would_replace_symlink";
        case SyncOutcome::ReplacedSymlink: return "replaced_symlink";
        case SyncOutcome::RefusedConflict: return "refused_conflict";
        case SyncOutcome::RefusedTypeMismatch: return "refused_type_mismatch";
        case SyncOutcome::RefusedUnsafeDirectoryReplace: return "refused_unsafe_directory_replace";
        case SyncOutcome::RefusedPathSafety: return "refused_path_safety";
        case SyncOutcome::Skipped: return "skipped";
        case SyncOutcome::Failed: return "failed";
    }
    return "unknown";
}

bool is_refused(SyncOutcome outcome) {
    switch (outcome) {
        case SyncOutcome::RefusedConflict:
        case SyncOutcome::RefusedTypeMismatch:
        case SyncOutcome::RefusedUnsafeDirectoryReplace:
        case SyncOutcome::RefusedPathSafety:
        case SyncOutcome::Failed:
            return true;
        default:
            return false;
    }
}

bool is_change(SyncOutcome outcome) {
    return outcome == SyncOutcome::Created || outcome == SyncOutcome::Updated ||
           outcome == SyncOutcome::ReplacedSymlink;
}

namespace {

std::string timestamp_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return ss.str();
}

// Removes a single filesystem object (regular file or symlink, or an EMPTY
// directory). Never recurses; a non-empty directory is left untouched and
// reported as such. This is the sole deletion primitive used anywhere in
// this file, structurally guaranteeing dotfile-manager never performs a
// recursive delete of user content.
struct RemoveResult {
    bool removed = false;
    bool non_empty_directory = false;
    std::string error;
};

RemoveResult remove_single(const fs::path& p) {
    RemoveResult r;
    std::error_code ec;
    auto info = inspect(p);
    if (info.kind == EntryKind::NotFound) {
        r.removed = true;  // already gone
        return r;
    }
    bool ok = fs::remove(p, ec);
    if (ec) {
        if (ec == std::errc::directory_not_empty) {
            r.non_empty_directory = true;
            r.error = "refusing: directory is not empty (dotfile-manager never deletes recursively): " + p.string();
        } else {
            r.error = "failed to remove '" + p.string() + "': " + ec.message();
        }
        return r;
    }
    r.removed = ok;
    return r;
}

// Places a copy of the current content at `home_abs` under
// <store_dir>/.dotfile-manager/safety-backups/<timestamp>/<home_relative>
// before a destructive replacement. Directories are copied recursively
// (files only, respecting ignore); regular files/symlinks are copied
// directly. Returns the backup path, or std::nullopt if there was nothing
// to back up (home_abs does not exist).
std::optional<fs::path> make_safety_backup(const fs::path& store_dir, const std::string& home_relative,
                                            const fs::path& home_abs, const IgnoreRules& ignore, bool dry_run,
                                            std::string* error) {
    auto info = inspect(home_abs);
    if (info.kind == EntryKind::NotFound) return std::nullopt;

    fs::path backup_root = store_dir / ".dotfile-manager" / "safety-backups" / timestamp_now();
    fs::path dest = backup_root / fs::path(home_relative);

    if (dry_run) return dest;

    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (ec) {
        if (error) *error = "failed to create safety backup directory: " + ec.message();
        return std::nullopt;
    }

    if (info.kind == EntryKind::Regular) {
        auto err = atomic_copy_file(home_abs, dest);
        if (err) {
            if (error) *error = "failed to create safety backup: " + *err;
            return std::nullopt;
        }
    } else if (info.kind == EntryKind::Symlink) {
        auto target = read_symlink_target(home_abs);
        if (target) {
            fs::create_symlink(*target, dest, ec);
            if (ec) {
                if (error) *error = "failed to snapshot symlink for safety backup: " + ec.message();
                return std::nullopt;
            }
        }
    } else if (info.kind == EntryKind::Directory) {
        auto err = copy_tree(home_abs, dest, ignore, /*dry_run=*/false, nullptr);
        if (err) {
            if (error) *error = "failed to create safety backup: " + *err;
            return std::nullopt;
        }
    }
    return dest;
}

}  // namespace

std::optional<std::string> copy_tree(const fs::path& src_dir, const fs::path& dest_dir, const IgnoreRules& ignore,
                                      bool dry_run, std::vector<std::string>* changed_files) {
    if (!is_directory_no_follow(src_dir)) return "source is not a directory: " + src_dir.string();

    if (!dry_run) {
        std::error_code ec;
        fs::create_directories(dest_dir, ec);
        if (ec) return "failed to create destination directory '" + dest_dir.string() + "': " + ec.message();
    }

    auto src_tree = list_tree(src_dir, ignore);
    for (const auto& [rel, node] : src_tree) {
        fs::path src_path = src_dir / fs::path(rel);
        fs::path dest_path = dest_dir / fs::path(rel);

        switch (node.kind) {
            case EntryKind::Directory: {
                if (!dry_run) {
                    std::error_code ec;
                    fs::create_directories(dest_path, ec);
                    if (ec) return "failed to create directory '" + dest_path.string() + "': " + ec.message();
                }
                break;
            }
            case EntryKind::Regular: {
                auto dest_info = inspect(dest_path);
                bool needs_write = true;
                if (dest_info.kind == EntryKind::Regular) {
                    std::string herr;
                    auto dest_hash = hash_file(dest_path, &herr);
                    needs_write = !(dest_hash && *dest_hash == node.hash);
                }
                if (needs_write) {
                    if (changed_files) changed_files->push_back(rel);
                    if (!dry_run) {
                        if (dest_info.kind != EntryKind::NotFound && dest_info.kind != EntryKind::Regular) {
                            auto rm = remove_single(dest_path);
                            if (!rm.removed) return rm.error.empty() ? "failed to replace non-regular destination: " + dest_path.string() : rm.error;
                        }
                        auto err = atomic_copy_file(src_path, dest_path);
                        if (err) return "failed to copy '" + src_path.string() + "' -> '" + dest_path.string() + "': " + *err;
                    }
                }
                break;
            }
            case EntryKind::Symlink: {
                auto dest_target = read_symlink_target(dest_path);
                bool needs_write = !(dest_target && *dest_target == node.symlink_target);
                if (needs_write) {
                    if (changed_files) changed_files->push_back(rel);
                    if (!dry_run) {
                        auto dest_info = inspect(dest_path);
                        if (dest_info.kind != EntryKind::NotFound) {
                            auto rm = remove_single(dest_path);
                            if (!rm.removed) return rm.error.empty() ? "failed to replace destination symlink: " + dest_path.string() : rm.error;
                        }
                        std::error_code ec;
                        fs::create_symlink(node.symlink_target, dest_path, ec);
                        if (ec) return "failed to create symlink '" + dest_path.string() + "': " + ec.message();
                    }
                }
                break;
            }
            default:
                break;  // special files are never copied
        }
    }
    return std::nullopt;
}

SyncResult sync_entry_home_to_store(const fs::path& home_dir, const fs::path& store_dir, const ManagedEntry& entry,
                                     const IgnoreRules& ignore, bool dry_run, bool force) {
    SyncResult result;

    auto home_r = resolve_within(home_dir, entry.home_path);
    auto store_r = resolve_within(store_dir, entry.store_path);
    if (!home_r.ok() || !store_r.ok()) {
        result.outcome = SyncOutcome::RefusedPathSafety;
        result.message = !home_r.ok() ? home_r.message : store_r.message;
        return result;
    }
    const fs::path& home_abs = home_r.resolved;
    const fs::path& store_abs = store_r.resolved;

    auto home_info = inspect(home_abs);
    auto store_info = inspect(store_abs);

    if (home_info.kind == EntryKind::NotFound) {
        result.outcome = SyncOutcome::Skipped;
        result.message = "home path does not exist; nothing to back up";
        return result;
    }
    if (!is_supported_kind(home_info.kind)) {
        result.outcome = SyncOutcome::RefusedConflict;
        result.message = "home path is a special file; dotfile-manager never backs up special files";
        return result;
    }

    if (home_info.kind == EntryKind::Symlink) {
        auto canon_home = try_canonical(home_abs);
        auto canon_store = try_canonical(store_abs);
        if (canon_home && canon_store && *canon_home == *canon_store) {
            result.outcome = SyncOutcome::NoChangeInSync;
            result.message = "home is already a symlink into the managed store";
            return result;
        }
        result.outcome = SyncOutcome::RefusedConflict;
        result.message = "home path is an unmanaged symlink; resolve manually before backing up";
        return result;
    }

    // home_info.kind is Regular or Directory from here.
    if (store_info.kind != EntryKind::NotFound && store_info.kind != home_info.kind) {
        if (!force) {
            result.outcome = SyncOutcome::RefusedTypeMismatch;
            result.message = "store copy type differs from home (store=" + dfm::to_string(store_info.kind) +
                              ", home=" + dfm::to_string(home_info.kind) + "); use --force to replace the store copy";
            return result;
        }
        if (dry_run) {
            result.outcome = SyncOutcome::WouldUpdate;
            result.message = "would replace store copy (type mismatch) with home content";
            return result;
        }
        auto rm = remove_single(store_abs);
        if (!rm.removed) {
            result.outcome = rm.non_empty_directory ? SyncOutcome::RefusedUnsafeDirectoryReplace : SyncOutcome::Failed;
            result.message = rm.error;
            return result;
        }
        store_info = inspect(store_abs);  // now NotFound
    }

    if (store_info.kind == EntryKind::NotFound) {
        if (dry_run) {
            result.outcome = SyncOutcome::WouldCreate;
            result.message = "would create store copy from home content";
            return result;
        }
        if (home_info.kind == EntryKind::Regular) {
            std::error_code ec;
            fs::create_directories(store_abs.parent_path(), ec);
            if (ec) {
                result.outcome = SyncOutcome::Failed;
                result.message = "failed to create store directory: " + ec.message();
                return result;
            }
            auto err = atomic_copy_file(home_abs, store_abs);
            if (err) {
                result.outcome = SyncOutcome::Failed;
                result.message = *err;
                return result;
            }
        } else {
            auto err = copy_tree(home_abs, store_abs, ignore, false, &result.changed_files);
            if (err) {
                result.outcome = SyncOutcome::Failed;
                result.message = *err;
                return result;
            }
        }
        result.outcome = SyncOutcome::Created;
        result.message = "created store copy from home content";
        return result;
    }

    // Both exist with matching kind: Regular or Directory.
    if (home_info.kind == EntryKind::Regular) {
        std::string ea, eb;
        auto h_home = hash_file(home_abs, &ea);
        auto h_store = hash_file(store_abs, &eb);
        if (!h_home || !h_store) {
            result.outcome = SyncOutcome::Failed;
            result.message = "failed to hash content: " + (ea.empty() ? eb : ea);
            return result;
        }
        if (*h_home == *h_store) {
            result.outcome = SyncOutcome::NoChangeInSync;
            result.message = "store copy already matches home content";
            return result;
        }
        if (dry_run) {
            result.outcome = SyncOutcome::WouldUpdate;
            result.message = "would update store copy from home content";
            return result;
        }
        auto err = atomic_copy_file(home_abs, store_abs);
        if (err) {
            result.outcome = SyncOutcome::Failed;
            result.message = *err;
            return result;
        }
        result.outcome = SyncOutcome::Updated;
        result.message = "updated store copy from home content";
        return result;
    }

    // Directory merge.
    std::vector<std::string> would_change;
    auto err = copy_tree(home_abs, store_abs, ignore, dry_run, &would_change);
    if (err) {
        result.outcome = SyncOutcome::Failed;
        result.message = *err;
        return result;
    }
    result.changed_files = would_change;
    if (would_change.empty()) {
        result.outcome = SyncOutcome::NoChangeInSync;
        result.message = "store copy already matches home content";
    } else if (dry_run) {
        result.outcome = SyncOutcome::WouldUpdate;
        result.message = std::to_string(would_change.size()) + " file(s) would be updated in the store copy";
    } else {
        result.outcome = SyncOutcome::Updated;
        result.message = std::to_string(would_change.size()) + " file(s) updated in the store copy";
    }
    return result;
}

namespace {

// Shared implementation for apply (live store) and restore (a snapshot):
// pushes content FROM source_root/entry.store_path TO home, honoring mode
// and destructive-replacement safety.
SyncResult apply_source_to_home(const fs::path& home_dir, const fs::path& source_root, const fs::path& store_dir,
                                 const ManagedEntry& entry, const IgnoreRules& ignore, bool dry_run, bool force) {
    SyncResult result;

    auto home_r = resolve_within(home_dir, entry.home_path);
    auto src_r = resolve_within(source_root, entry.store_path);
    if (!home_r.ok() || !src_r.ok()) {
        result.outcome = SyncOutcome::RefusedPathSafety;
        result.message = !home_r.ok() ? home_r.message : src_r.message;
        return result;
    }
    const fs::path& home_abs = home_r.resolved;
    const fs::path& src_abs = src_r.resolved;

    auto src_info = inspect(src_abs);
    if (src_info.kind == EntryKind::NotFound) {
        result.outcome = SyncOutcome::Skipped;
        result.message = "nothing in the source to apply";
        return result;
    }
    if (!is_supported_kind(src_info.kind)) {
        result.outcome = SyncOutcome::RefusedConflict;
        result.message = "store content is a special file; refusing to apply it";
        return result;
    }

    auto home_info = inspect(home_abs);

    // -------------------- Symlink mode --------------------
    if (entry.mode == ManageMode::Symlink) {
        fs::path link_target = try_canonical(src_abs).value_or(src_abs);

        if (home_info.kind == EntryKind::NotFound) {
            if (dry_run) {
                result.outcome = SyncOutcome::WouldCreate;
                result.message = "would create symlink to the managed store";
                return result;
            }
            std::error_code ec;
            fs::create_directories(home_abs.parent_path(), ec);
            if (ec) {
                result.outcome = SyncOutcome::Failed;
                result.message = "failed to create parent directory: " + ec.message();
                return result;
            }
            fs::create_symlink(link_target, home_abs, ec);
            if (ec) {
                result.outcome = SyncOutcome::Failed;
                result.message = "failed to create symlink: " + ec.message();
                return result;
            }
            result.outcome = SyncOutcome::Created;
            result.message = "created symlink -> " + link_target.string();
            return result;
        }

        if (home_info.kind == EntryKind::Symlink) {
            auto canon_home = try_canonical(home_abs);
            if (canon_home && *canon_home == link_target) {
                result.outcome = SyncOutcome::NoChangeInSync;
                result.message = "symlink already points at the managed store";
                return result;
            }
            if (dry_run) {
                result.outcome = SyncOutcome::WouldReplaceSymlink;
                result.message = "would relink to the managed store (currently points elsewhere)";
                return result;
            }
            if (!force) {
                result.outcome = SyncOutcome::RefusedConflict;
                result.message = "existing symlink points elsewhere; use --force to relink";
                return result;
            }
            auto rm = remove_single(home_abs);
            if (!rm.removed) {
                result.outcome = SyncOutcome::Failed;
                result.message = rm.error;
                return result;
            }
            std::error_code ec;
            fs::create_symlink(link_target, home_abs, ec);
            if (ec) {
                result.outcome = SyncOutcome::Failed;
                result.message = "failed to create symlink: " + ec.message();
                return result;
            }
            result.outcome = SyncOutcome::ReplacedSymlink;
            result.message = "relinked -> " + link_target.string();
            return result;
        }

        // home exists with real content: replace-with-symlink safety path.
        if (dry_run) {
            result.outcome = SyncOutcome::WouldReplaceSymlink;
            result.message = "would back up existing home content, then replace it with a symlink";
            return result;
        }
        if (!force) {
            result.outcome = SyncOutcome::RefusedConflict;
            result.message = "home path has real content; use --force to back it up and replace it with a symlink";
            return result;
        }
        if (home_info.kind == EntryKind::Directory) {
            std::string berr;
            auto backup = make_safety_backup(store_dir, entry.home_path, home_abs, ignore, false, &berr);
            if (!backup) {
                result.outcome = SyncOutcome::Failed;
                result.message = berr;
                return result;
            }
            auto rm = remove_single(home_abs);
            if (!rm.removed) {
                result.outcome = rm.non_empty_directory ? SyncOutcome::RefusedUnsafeDirectoryReplace : SyncOutcome::Failed;
                result.message = rm.non_empty_directory
                                      ? rm.error + " (a safety backup of readable content was created at " +
                                            backup->string() + ")"
                                      : rm.error;
                return result;
            }
            std::error_code ec;
            fs::create_symlink(link_target, home_abs, ec);
            if (ec) {
                result.outcome = SyncOutcome::Failed;
                result.message = "failed to create symlink: " + ec.message();
                return result;
            }
            result.outcome = SyncOutcome::ReplacedSymlink;
            result.message = "backed up previous directory to " + backup->string() +
                              " and replaced it with a symlink -> " + link_target.string();
            return result;
        }
        // Regular file.
        std::string berr;
        auto backup = make_safety_backup(store_dir, entry.home_path, home_abs, ignore, false, &berr);
        if (!backup) {
            result.outcome = SyncOutcome::Failed;
            result.message = berr;
            return result;
        }
        auto rm = remove_single(home_abs);
        if (!rm.removed) {
            result.outcome = SyncOutcome::Failed;
            result.message = rm.error;
            return result;
        }
        std::error_code ec;
        fs::create_symlink(link_target, home_abs, ec);
        if (ec) {
            result.outcome = SyncOutcome::Failed;
            result.message = "failed to create symlink: " + ec.message();
            return result;
        }
        result.outcome = SyncOutcome::ReplacedSymlink;
        result.message =
            "backed up previous file to " + backup->string() + " and replaced it with a symlink -> " + link_target.string();
        return result;
    }

    // -------------------- Copy mode --------------------
    if (home_info.kind == EntryKind::NotFound) {
        if (dry_run) {
            result.outcome = SyncOutcome::WouldCreate;
            result.message = "would create home content from the managed store";
            return result;
        }
        std::error_code ec;
        fs::create_directories(home_abs.parent_path(), ec);
        if (ec) {
            result.outcome = SyncOutcome::Failed;
            result.message = "failed to create parent directory: " + ec.message();
            return result;
        }
        if (src_info.kind == EntryKind::Regular) {
            auto err = atomic_copy_file(src_abs, home_abs);
            if (err) {
                result.outcome = SyncOutcome::Failed;
                result.message = *err;
                return result;
            }
        } else {
            auto err = copy_tree(src_abs, home_abs, ignore, false, &result.changed_files);
            if (err) {
                result.outcome = SyncOutcome::Failed;
                result.message = *err;
                return result;
            }
        }
        result.outcome = SyncOutcome::Created;
        result.message = "created home content from the managed store";
        return result;
    }

    bool kind_mismatch = home_info.kind != src_info.kind;  // symlink counts as mismatch here too
    if (kind_mismatch) {
        // If home is a non-empty directory, remove_single() below will
        // structurally refuse to delete it (dotfile-manager never uses
        // remove_all()), so no separate emptiness pre-check is needed here.
        if (dry_run) {
            result.outcome = SyncOutcome::WouldUpdate;
            result.message = "would replace home content (type differs from store)";
            return result;
        }
        if (!force) {
            result.outcome = SyncOutcome::RefusedTypeMismatch;
            result.message = "home type differs from store (home=" + dfm::to_string(home_info.kind) +
                              ", store=" + dfm::to_string(src_info.kind) + "); use --force to replace";
            return result;
        }
        std::string berr;
        auto backup = make_safety_backup(store_dir, entry.home_path, home_abs, ignore, false, &berr);
        if (!backup) {
            result.outcome = SyncOutcome::Failed;
            result.message = berr;
            return result;
        }
        auto rm = remove_single(home_abs);
        if (!rm.removed) {
            result.outcome = rm.non_empty_directory ? SyncOutcome::RefusedUnsafeDirectoryReplace : SyncOutcome::Failed;
            result.message = rm.error;
            return result;
        }
        if (src_info.kind == EntryKind::Regular) {
            auto err = atomic_copy_file(src_abs, home_abs);
            if (err) {
                result.outcome = SyncOutcome::Failed;
                result.message = *err;
                return result;
            }
        } else {
            auto err2 = copy_tree(src_abs, home_abs, ignore, false, &result.changed_files);
            if (err2) {
                result.outcome = SyncOutcome::Failed;
                result.message = *err2;
                return result;
            }
        }
        result.outcome = SyncOutcome::Updated;
        result.message = "backed up previous content to " + backup->string() +
                          " and replaced it with the managed store version";
        return result;
    }

    if (src_info.kind == EntryKind::Regular) {
        std::string ea, eb;
        auto h_src = hash_file(src_abs, &ea);
        auto h_home = hash_file(home_abs, &eb);
        if (!h_src || !h_home) {
            result.outcome = SyncOutcome::Failed;
            result.message = "failed to hash content: " + (ea.empty() ? eb : ea);
            return result;
        }
        if (*h_src == *h_home) {
            result.outcome = SyncOutcome::NoChangeInSync;
            result.message = "home already matches the managed store";
            return result;
        }
        if (dry_run) {
            result.outcome = SyncOutcome::WouldUpdate;
            result.message = "would update home content from the managed store (a safety backup would be made)";
            return result;
        }
        if (!force) {
            result.outcome = SyncOutcome::RefusedConflict;
            result.message = "home content differs from the managed store; use --force to apply (a safety backup will be made)";
            return result;
        }
        std::string berr;
        auto backup = make_safety_backup(store_dir, entry.home_path, home_abs, ignore, false, &berr);
        if (!backup) {
            result.outcome = SyncOutcome::Failed;
            result.message = berr;
            return result;
        }
        auto err = atomic_copy_file(src_abs, home_abs);
        if (err) {
            result.outcome = SyncOutcome::Failed;
            result.message = *err;
            return result;
        }
        result.outcome = SyncOutcome::Updated;
        result.message = "updated home content from the managed store (previous version backed up to " +
                          backup->string() + ")";
        return result;
    }

    // Directory merge under copy mode.
    std::vector<std::string> would_change;
    auto probe_err = copy_tree(src_abs, home_abs, ignore, true, &would_change);
    if (probe_err) {
        result.outcome = SyncOutcome::Failed;
        result.message = *probe_err;
        return result;
    }
    if (would_change.empty()) {
        result.outcome = SyncOutcome::NoChangeInSync;
        result.message = "home already matches the managed store";
        return result;
    }
    if (dry_run) {
        result.outcome = SyncOutcome::WouldUpdate;
        result.changed_files = would_change;
        result.message = std::to_string(would_change.size()) + " file(s) would be updated under home";
        return result;
    }
    if (!force) {
        result.outcome = SyncOutcome::RefusedConflict;
        result.changed_files = would_change;
        result.message = std::to_string(would_change.size()) +
                          " file(s) differ under home; use --force to apply (safety backups will be made for overwritten files)";
        return result;
    }
    // Back up files that will actually be overwritten (already exist under home).
    for (const auto& rel : would_change) {
        fs::path h = home_abs / fs::path(rel);
        if (exists_no_follow(h)) {
            std::string berr;
            auto backup = make_safety_backup(store_dir, entry.home_path + "/" + rel, h, ignore, false, &berr);
            // Best-effort: a failed safety backup for one file should not
            // silently proceed to overwrite it.
            if (!backup) {
                result.outcome = SyncOutcome::Failed;
                result.message = "failed to create safety backup for '" + rel + "': " + berr;
                return result;
            }
        }
    }
    auto err = copy_tree(src_abs, home_abs, ignore, false, &result.changed_files);
    if (err) {
        result.outcome = SyncOutcome::Failed;
        result.message = *err;
        return result;
    }
    result.outcome = SyncOutcome::Updated;
    result.message = std::to_string(result.changed_files.size()) + " file(s) updated under home (previous versions backed up)";
    return result;
}

}  // namespace

SyncResult sync_entry_store_to_home(const fs::path& home_dir, const fs::path& store_dir, const ManagedEntry& entry,
                                     const IgnoreRules& ignore, bool dry_run, bool force) {
    return apply_source_to_home(home_dir, store_dir, store_dir, entry, ignore, dry_run, force);
}

std::optional<SnapshotInfo> create_snapshot(const fs::path& store_dir, const std::vector<ManagedEntry>& entries,
                                             const IgnoreRules& ignore, bool dry_run, std::string* error) {
    std::string id = timestamp_now();
    fs::path snapshot_dir = store_dir / ".dotfile-manager" / "snapshots" / id;

    if (dry_run) return SnapshotInfo{id, snapshot_dir};

    std::error_code ec;
    fs::create_directories(snapshot_dir, ec);
    if (ec) {
        if (error) *error = "failed to create snapshot directory: " + ec.message();
        return std::nullopt;
    }

    for (const auto& entry : entries) {
        auto store_r = resolve_within(store_dir, entry.store_path);
        if (!store_r.ok()) continue;  // unsafe entries are skipped, not fatal for the whole snapshot
        auto info = inspect(store_r.resolved);
        fs::path dest = snapshot_dir / fs::path(entry.store_path);
        if (info.kind == EntryKind::Regular) {
            fs::create_directories(dest.parent_path(), ec);
            atomic_copy_file(store_r.resolved, dest);
        } else if (info.kind == EntryKind::Directory) {
            copy_tree(store_r.resolved, dest, ignore, false, nullptr);
        } else if (info.kind == EntryKind::Symlink) {
            fs::create_directories(dest.parent_path(), ec);
            auto target = read_symlink_target(store_r.resolved);
            if (target) fs::create_symlink(*target, dest, ec);
        }
    }

    return SnapshotInfo{id, snapshot_dir};
}

std::vector<SnapshotInfo> list_snapshots(const fs::path& store_dir) {
    std::vector<SnapshotInfo> out;
    fs::path snapshots_dir = store_dir / ".dotfile-manager" / "snapshots";
    std::error_code ec;
    if (!fs::is_directory(snapshots_dir, ec)) return out;

    for (const auto& child : fs::directory_iterator(snapshots_dir, fs::directory_options::skip_permission_denied, ec)) {
        if (is_directory_no_follow(child.path())) {
            out.push_back(SnapshotInfo{child.path().filename().string(), child.path()});
        }
    }
    std::sort(out.begin(), out.end(), [](const SnapshotInfo& a, const SnapshotInfo& b) { return a.id > b.id; });
    return out;
}

SyncResult restore_entry_from_snapshot(const fs::path& home_dir, const fs::path& store_dir,
                                        const std::string& snapshot_id, const ManagedEntry& entry,
                                        const IgnoreRules& ignore, bool dry_run, bool force) {
    auto snap_r = resolve_within(store_dir, ".dotfile-manager/snapshots/" + snapshot_id);
    if (!snap_r.ok()) {
        SyncResult result;
        result.outcome = SyncOutcome::RefusedPathSafety;
        result.message = "invalid snapshot id: " + snap_r.message;
        return result;
    }
    if (!is_directory_no_follow(snap_r.resolved)) {
        SyncResult result;
        result.outcome = SyncOutcome::Failed;
        result.message = "snapshot not found: " + snapshot_id;
        return result;
    }
    return apply_source_to_home(home_dir, snap_r.resolved, store_dir, entry, ignore, dry_run, force);
}

}  // namespace dfm
