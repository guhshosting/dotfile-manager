#include "dfm/commands.hpp"

#include "dfm/atomic_io.hpp"
#include "dfm/backup.hpp"
#include "dfm/compare.hpp"
#include "dfm/config.hpp"
#include "dfm/diff_engine.hpp"
#include "dfm/discovery.hpp"
#include "dfm/env.hpp"
#include "dfm/exit_codes.hpp"
#include "dfm/file_types.hpp"
#include "dfm/hashing.hpp"
#include "dfm/output.hpp"
#include "dfm/path_safety.hpp"
#include "dfm/version.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <sstream>

namespace dfm {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string display_home_path(const std::string& rel) { return "~/" + rel; }

// Resolves the store directory for read-only purposes. Returns canonical
// path if it exists, or std::nullopt (callers should treat every entry as
// MISSING_STORE).
EntryStatusResult status_for_entry(const AppContext& ctx, const ManagedEntry& entry) {
    if (!ctx.store_exists) {
        EntryStatusResult r;
        r.status = EntryStatus::MissingStore;
        r.detail = "the managed store directory does not exist yet";
        return r;
    }
    return compute_status(ctx.home_dir, ctx.store_dir, entry, ctx.ignore);
}

int exit_code_for_statuses(const std::vector<EntryStatus>& statuses) {
    bool any_conflict = false, any_unknown = false, any_diff = false;
    for (auto s : statuses) {
        switch (s) {
            case EntryStatus::Conflict:
            case EntryStatus::TypeMismatch:
            case EntryStatus::SymlinkMismatch:
                any_conflict = true;
                break;
            case EntryStatus::Unknown:
                any_unknown = true;
                break;
            case EntryStatus::Modified:
            case EntryStatus::MissingHome:
            case EntryStatus::MissingStore:
                any_diff = true;
                break;
            case EntryStatus::InSync:
                break;
        }
    }
    if (any_conflict) return kExitUnresolvedConflict;
    if (any_unknown) return kExitIoFailure;
    if (any_diff) return kExitDifferences;
    return kExitSuccess;
}

std::vector<ManagedEntry*> select_entries(AppContext& ctx, const std::vector<std::string>& positional,
                                           std::string* error) {
    std::vector<ManagedEntry*> selected;
    if (positional.empty()) {
        for (auto& e : ctx.config.entries) selected.push_back(&e);
        return selected;
    }
    for (const auto& p : positional) {
        std::string err;
        auto rel = resolve_user_path_to_home_relative(ctx.home_dir, p, &err);
        ManagedEntry* found = nullptr;
        if (rel) found = ctx.config.find_by_home_path(*rel);
        if (!found) {
            if (error) *error = "not a managed entry: " + p;
            return {};
        }
        selected.push_back(found);
    }
    return selected;
}

}  // namespace

// ---------------------------------------------------------------------------
// discover
// ---------------------------------------------------------------------------
CommandResult cmd_discover(AppContext& ctx, const ParsedArgs& args) {
    CommandResult result;
    if (!args.positional.empty()) {
        result.exit_code = kExitUsageError;
        result.output = "error: 'discover' does not take positional arguments\n";
        return result;
    }

    auto found = discover(ctx.home_dir);

    if (ctx.global.format == OutputFormat::Json) {
        json arr = json::array();
        for (const auto& d : found) {
            json item;
            item["relative_path"] = d.relative_path;
            item["home_path"] = display_home_path(d.relative_path);
            item["kind"] = to_string(d.kind);
            item["sensitive"] = d.sensitive;
            item["note"] = d.note;
            item["already_managed"] = ctx.config.find_by_home_path(d.relative_path) != nullptr;
            arr.push_back(std::move(item));
        }
        json root;
        root["discovered"] = arr;
        root["count"] = found.size();
        result.output = root.dump(2) + "\n";
    } else if (ctx.global.format == OutputFormat::Markdown) {
        Table table;
        table.headers = {"Path", "Type", "Sensitive", "Note"};
        for (const auto& d : found) {
            table.rows.push_back({display_home_path(d.relative_path), to_string(d.kind), d.sensitive ? "yes" : "no",
                                   ctx.config.find_by_home_path(d.relative_path) ? "already managed" : d.note});
        }
        std::ostringstream out;
        out << "# Discovered dotfiles\n\n" << table.render_markdown();
        result.output = out.str();
    } else {
        std::ostringstream out;
        out << "Discovered " << found.size() << " candidate entr" << (found.size() == 1 ? "y" : "ies")
            << " under bounded, safe locations (" << ctx.home_dir.string() << " and " << ctx.home_dir.string()
            << "/.config, depth-limited).\n\n";
        for (const auto& d : found) {
            bool managed = ctx.config.find_by_home_path(d.relative_path) != nullptr;
            out << "  " << display_home_path(d.relative_path) << "  [" << to_string(d.kind) << "]";
            if (d.sensitive) out << "  SENSITIVE";
            if (managed) out << "  (already managed)";
            out << "\n";
        }
        if (found.empty()) out << "  (none found)\n";
        out << "\nSensitive entries require --allow-sensitive on 'add'. Nothing was read or copied.\n";
        result.output = out.str();
    }
    result.exit_code = kExitSuccess;
    return result;
}

// ---------------------------------------------------------------------------
// list
// ---------------------------------------------------------------------------
CommandResult cmd_list(const AppContext& ctx, const ParsedArgs& args) {
    CommandResult result;
    if (!args.positional.empty()) {
        result.exit_code = kExitUsageError;
        result.output = "error: 'list' does not take positional arguments\n";
        return result;
    }

    if (args.flags.count("snapshots")) {
        auto snaps = ctx.store_exists ? list_snapshots(ctx.store_dir) : std::vector<SnapshotInfo>{};
        if (ctx.global.format == OutputFormat::Json) {
            json arr = json::array();
            for (const auto& s : snaps) {
                json item;
                item["id"] = s.id;
                item["path"] = s.path.string();
                arr.push_back(std::move(item));
            }
            json root;
            root["snapshots"] = arr;
            root["count"] = snaps.size();
            result.output = root.dump(2) + "\n";
        } else if (ctx.global.format == OutputFormat::Markdown) {
            Table table;
            table.headers = {"Snapshot ID", "Path"};
            for (const auto& s : snaps) table.rows.push_back({s.id, s.path.string()});
            std::ostringstream out;
            out << "# Snapshots\n\n" << table.render_markdown();
            result.output = out.str();
        } else {
            std::ostringstream out;
            out << "Snapshots: " << snaps.size() << "\n";
            for (const auto& s : snaps) out << "  " << s.id << "  (" << s.path.string() << ")\n";
            result.output = out.str();
        }
        result.exit_code = kExitSuccess;
        return result;
    }

    if (ctx.global.format == OutputFormat::Json) {
        json arr = json::array();
        for (const auto& e : ctx.config.entries) {
            json item;
            item["home_path"] = e.home_path;
            item["store_path"] = e.store_path;
            item["mode"] = to_string(e.mode);
            item["tags"] = e.tags;
            arr.push_back(std::move(item));
        }
        json root;
        root["store"] = ctx.config.store;
        root["entries"] = arr;
        root["count"] = ctx.config.entries.size();
        result.output = root.dump(2) + "\n";
    } else if (ctx.global.format == OutputFormat::Markdown) {
        Table table;
        table.headers = {"Home Path", "Store Path", "Mode", "Tags"};
        for (const auto& e : ctx.config.entries) {
            std::string tags;
            for (std::size_t i = 0; i < e.tags.size(); ++i) tags += (i ? ", " : "") + e.tags[i];
            table.rows.push_back({display_home_path(e.home_path), e.store_path, to_string(e.mode), tags});
        }
        std::ostringstream out;
        out << "# Managed entries\n\nStore: `" << ctx.config.store << "`\n\n" << table.render_markdown();
        result.output = out.str();
    } else {
        std::ostringstream out;
        out << "Store:\n  " << ctx.config.store << "\n\n";
        out << "Managed entries: " << ctx.config.entries.size() << "\n\n";
        for (const auto& e : ctx.config.entries) {
            out << "  " << display_home_path(e.home_path) << "  ->  " << e.store_path << "  [" << to_string(e.mode)
                << "]";
            if (!e.tags.empty()) {
                out << "  tags:";
                for (const auto& t : e.tags) out << " " << t;
            }
            out << "\n";
        }
        if (ctx.config.entries.empty()) out << "  (no managed entries yet - see 'dotfile-manager discover')\n";
        result.output = out.str();
    }
    result.exit_code = kExitSuccess;
    return result;
}

// ---------------------------------------------------------------------------
// add
// ---------------------------------------------------------------------------
CommandResult cmd_add(AppContext& ctx, const ParsedArgs& args) {
    CommandResult result;
    if (args.positional.size() != 1) {
        result.exit_code = kExitUsageError;
        result.output = "error: 'add' requires exactly one path argument\n";
        return result;
    }

    std::string err;
    auto home_rel = resolve_user_path_to_home_relative(ctx.home_dir, args.positional[0], &err);
    if (!home_rel) {
        result.exit_code = kExitPathSafetyError;
        result.output = "error: " + err + "\n";
        return result;
    }

    auto home_r = resolve_within(ctx.home_dir, *home_rel);
    if (!home_r.ok()) {
        result.exit_code = kExitPathSafetyError;
        result.output = "error: " + home_r.message + "\n";
        return result;
    }
    auto home_info = inspect(home_r.resolved);
    if (home_info.kind == EntryKind::NotFound) {
        result.exit_code = kExitUsageError;
        result.output = "error: no such file or directory: " + args.positional[0] + "\n";
        return result;
    }
    if (!is_supported_kind(home_info.kind)) {
        result.exit_code = kExitPathSafetyError;
        result.output = "error: refusing to manage a special file (" + dfm::to_string(home_info.kind) + ")\n";
        return result;
    }

    if (is_sensitive_path(*home_rel) && !args.flags.count("allow-sensitive")) {
        result.exit_code = kExitPathSafetyError;
        result.output = "error: '" + display_home_path(*home_rel) +
                         "' is classified as sensitive; re-run with --allow-sensitive to add it explicitly\n";
        return result;
    }

    if (ctx.config.find_by_home_path(*home_rel) != nullptr) {
        result.exit_code = kExitUnresolvedConflict;
        result.output = "error: '" + display_home_path(*home_rel) +
                         "' is already managed; use 'backup'/'apply' to sync it or 'remove' first\n";
        return result;
    }

    std::string mode_str = args.options.count("mode") ? args.options.at("mode") : "copy";
    auto mode = manage_mode_from_string(mode_str);
    if (!mode) {
        result.exit_code = kExitUsageError;
        result.output = "error: invalid --mode value '" + mode_str + "' (expected copy|symlink)\n";
        return result;
    }

    std::string store_path = args.options.count("store-path") ? args.options.at("store-path") : *home_rel;
    auto store_path_check = validate_relative_path(store_path);
    if (!store_path_check.ok()) {
        result.exit_code = kExitPathSafetyError;
        result.output = "error: invalid --store-path: " + store_path_check.message + "\n";
        return result;
    }
    store_path = store_path_check.resolved.generic_string();

    for (const auto& e : ctx.config.entries) {
        if (e.store_path == store_path) {
            result.exit_code = kExitUnresolvedConflict;
            result.output = "error: store path '" + store_path + "' is already used by managed entry '" +
                             display_home_path(e.home_path) + "'\n";
            return result;
        }
    }

    ManagedEntry entry;
    entry.home_path = *home_rel;
    entry.store_path = store_path;
    entry.mode = *mode;
    if (args.options.count("tags")) entry.tags = split_csv(args.options.at("tags"));

    if (ctx.global.dry_run) {
        std::ostringstream out;
        out << "DRY RUN: would add '" << display_home_path(entry.home_path) << "' as " << to_string(entry.mode)
            << "-managed (store path: " << entry.store_path << ")\n";
        result.output = out.str();
        result.exit_code = kExitSuccess;
        return result;
    }

    std::string ensure_err;
    auto store_canon = ensure_directory_canonical(ctx.store_dir, /*create_if_missing=*/true, &ensure_err);
    if (!store_canon) {
        result.exit_code = kExitIoFailure;
        result.output = "error: failed to prepare store directory: " + ensure_err + "\n";
        return result;
    }
    ctx.store_dir = *store_canon;
    ctx.store_exists = true;

    auto sync = sync_entry_home_to_store(ctx.home_dir, ctx.store_dir, entry, ctx.ignore, false, ctx.global.force);
    if (is_refused(sync.outcome)) {
        result.exit_code = sync.outcome == SyncOutcome::RefusedPathSafety ? kExitPathSafetyError
                            : sync.outcome == SyncOutcome::Failed          ? kExitIoFailure
                                                                            : kExitUnresolvedConflict;
        result.output = "error: could not copy into the store: " + sync.message + "\n";
        return result;
    }

    ctx.config.entries.push_back(entry);
    auto save_err = save_config_file(ctx.config_path, ctx.config);
    if (save_err) {
        result.exit_code = kExitIoFailure;
        result.output = "error: failed to save configuration: " + *save_err + "\n";
        return result;
    }

    std::ostringstream out;
    out << "Added '" << display_home_path(entry.home_path) << "' (" << to_string(entry.mode)
        << ", store path: " << entry.store_path << "). " << sync.message << "\n";
    result.output = out.str();
    result.exit_code = kExitSuccess;
    return result;
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------
CommandResult cmd_remove(AppContext& ctx, const ParsedArgs& args) {
    CommandResult result;
    if (args.positional.size() != 1) {
        result.exit_code = kExitUsageError;
        result.output = "error: 'remove' requires exactly one path argument\n";
        return result;
    }

    std::string err;
    auto home_rel = resolve_user_path_to_home_relative(ctx.home_dir, args.positional[0], &err);
    if (!home_rel) {
        result.exit_code = kExitPathSafetyError;
        result.output = "error: " + err + "\n";
        return result;
    }

    auto* entry_ptr = ctx.config.find_by_home_path(*home_rel);
    if (!entry_ptr) {
        result.exit_code = kExitUsageError;
        result.output = "error: '" + display_home_path(*home_rel) + "' is not a managed entry\n";
        return result;
    }
    ManagedEntry entry = *entry_ptr;  // copy before we mutate the vector

    bool purge = args.flags.count("purge-store") > 0;

    if (ctx.global.dry_run) {
        std::ostringstream out;
        out << "DRY RUN: would remove '" << display_home_path(entry.home_path) << "' from management metadata\n";
        if (purge) out << "DRY RUN: would also delete the store copy at '" << entry.store_path << "' (only if empty/regular)\n";
        result.output = out.str();
        result.exit_code = kExitSuccess;
        return result;
    }

    std::string purge_note;
    int purge_warn = kExitSuccess;
    if (purge && ctx.store_exists) {
        auto store_r = resolve_within(ctx.store_dir, entry.store_path);
        if (store_r.ok()) {
            std::error_code ec;
            bool removed = fs::remove(store_r.resolved, ec);
            if (ec) {
                if (ec == std::errc::directory_not_empty) {
                    purge_note = "note: store copy is a non-empty directory and was NOT deleted (dotfile-manager "
                                 "never deletes recursively); remove it manually if desired: " +
                                 store_r.resolved.string();
                    purge_warn = kExitDifferences;
                } else {
                    purge_note = "warning: failed to delete store copy: " + ec.message();
                    purge_warn = kExitIoFailure;
                }
            } else if (removed) {
                purge_note = "store copy deleted: " + store_r.resolved.string();
            } else {
                purge_note = "note: no store copy existed to delete";
            }
        } else {
            purge_note = "warning: could not safely resolve store path for purge: " + store_r.message;
            purge_warn = kExitPathSafetyError;
        }
    }

    ctx.config.entries.erase(std::remove_if(ctx.config.entries.begin(), ctx.config.entries.end(),
                                             [&](const ManagedEntry& e) { return e.home_path == entry.home_path; }),
                              ctx.config.entries.end());
    auto save_err = save_config_file(ctx.config_path, ctx.config);
    if (save_err) {
        result.exit_code = kExitIoFailure;
        result.output = "error: failed to save configuration: " + *save_err + "\n";
        return result;
    }

    std::ostringstream out;
    out << "Removed '" << display_home_path(entry.home_path)
        << "' from management metadata. The file at that path (if any) was not touched.\n";
    if (!purge_note.empty()) out << purge_note << "\n";
    result.output = out.str();
    result.exit_code = purge_warn;
    return result;
}

// ---------------------------------------------------------------------------
// status
// ---------------------------------------------------------------------------
CommandResult cmd_status(AppContext& ctx, const ParsedArgs& args) {
    CommandResult result;
    std::string err;
    auto selected = select_entries(ctx, args.positional, &err);
    // (false positive below: select_entries() returns an empty vector both
    // when there are simply zero managed entries -- err left empty -- and
    // when an explicit path argument didn't match a managed entry -- err
    // set. cppcheck's interprocedural analysis does not track the
    // std::string* out-parameter across the call, so both branches are
    // needed to tell these two cases apart at runtime.)
    // cppcheck-suppress knownConditionTrueFalse
    if (selected.empty() && !err.empty()) {
        result.exit_code = kExitUsageError;
        result.output = "error: " + err + "\n";
        return result;
    }

    std::vector<std::pair<const ManagedEntry*, EntryStatusResult>> statuses;
    for (const auto* e : selected) statuses.emplace_back(e, status_for_entry(ctx, *e));

    std::vector<EntryStatus> just_status;
    for (auto& [e, s] : statuses) {
        (void)e;
        just_status.push_back(s.status);
    }
    result.exit_code = exit_code_for_statuses(just_status);

    auto group = [&](EntryStatus st) {
        std::vector<const ManagedEntry*> out;
        for (auto& [e, s] : statuses)
            if (s.status == st) out.push_back(e);
        return out;
    };

    if (ctx.global.format == OutputFormat::Json) {
        json arr = json::array();
        for (auto& [e, s] : statuses) {
            json item;
            item["home_path"] = e->home_path;
            item["store_path"] = e->store_path;
            item["mode"] = to_string(e->mode);
            item["status"] = to_string(s.status);
            item["detail"] = s.detail;
            if (s.is_directory_entry) {
                json changes = json::array();
                for (const auto& c : s.directory_changes) {
                    changes.push_back({{"path", c.relative_path}, {"change", to_string(c.change)}});
                }
                item["directory_changes"] = changes;
            }
            arr.push_back(std::move(item));
        }
        json root;
        root["entries"] = arr;
        json summary;
        for (auto st : {EntryStatus::InSync, EntryStatus::Modified, EntryStatus::MissingHome,
                         EntryStatus::MissingStore, EntryStatus::TypeMismatch, EntryStatus::SymlinkMismatch,
                         EntryStatus::Conflict, EntryStatus::Unknown}) {
            summary[to_string(st)] = static_cast<int>(group(st).size());
        }
        root["summary"] = summary;
        result.output = root.dump(2) + "\n";
        return result;
    }

    if (ctx.global.format == OutputFormat::Markdown) {
        Table table;
        table.headers = {"Home Path", "Status", "Detail"};
        for (auto& [e, s] : statuses) table.rows.push_back({display_home_path(e->home_path), to_string(s.status), s.detail});
        std::ostringstream out;
        out << "# Status\n\n" << table.render_markdown();
        result.output = out.str();
        return result;
    }

    std::ostringstream out;
    out << "Dotfile Manager " << kVersion << "\n\n";
    out << "Store:\n  " << ctx.config.store << "\n\n";
    out << "Managed entries: " << ctx.config.entries.size() << "\n\n";
    out << "Status\n------\n\n";
    for (auto st : {EntryStatus::InSync, EntryStatus::Modified, EntryStatus::MissingHome, EntryStatus::MissingStore,
                    EntryStatus::TypeMismatch, EntryStatus::SymlinkMismatch, EntryStatus::Conflict,
                    EntryStatus::Unknown}) {
        auto entries_in_group = group(st);
        if (entries_in_group.empty()) continue;
        out << to_string(st) << "\n";
        for (const auto* e : entries_in_group) {
            out << "  " << display_home_path(e->home_path) << "\n";
        }
        out << "\n";
    }
    out << "Summary:\n";
    out << "  In sync: " << group(EntryStatus::InSync).size() << "\n";
    out << "  Modified: " << group(EntryStatus::Modified).size() << "\n";
    out << "  Missing home: " << group(EntryStatus::MissingHome).size() << "\n";
    out << "  Missing store: " << group(EntryStatus::MissingStore).size() << "\n";
    out << "  Type mismatch: " << group(EntryStatus::TypeMismatch).size() << "\n";
    out << "  Symlink mismatch: " << group(EntryStatus::SymlinkMismatch).size() << "\n";
    out << "  Conflicts: " << group(EntryStatus::Conflict).size() << "\n";
    out << "  Unknown: " << group(EntryStatus::Unknown).size() << "\n";
    result.output = out.str();
    return result;
}

// ---------------------------------------------------------------------------
// diff
// ---------------------------------------------------------------------------
CommandResult cmd_diff(AppContext& ctx, const ParsedArgs& args) {
    CommandResult result;
    if (args.positional.size() != 1) {
        result.exit_code = kExitUsageError;
        result.output = "error: 'diff' requires exactly one path argument\n";
        return result;
    }
    std::string err;
    auto selected = select_entries(ctx, args.positional, &err);
    if (selected.size() != 1) {
        result.exit_code = kExitUsageError;
        result.output = "error: " + err + "\n";
        return result;
    }
    const ManagedEntry& entry = *selected[0];
    auto status = status_for_entry(ctx, entry);

    if (status.status == EntryStatus::InSync) {
        result.exit_code = kExitSuccess;
        result.output = ctx.global.format == OutputFormat::Json ? std::string("{\"identical\": true}\n")
                                                                  : std::string("No differences.\n");
        return result;
    }
    if (status.status != EntryStatus::Modified) {
        result.exit_code = (status.status == EntryStatus::MissingHome || status.status == EntryStatus::MissingStore)
                                ? kExitDifferences
                                : kExitUnresolvedConflict;
        std::ostringstream out;
        out << to_string(status.status) << ": " << status.detail << "\n";
        result.output = out.str();
        return result;
    }

    result.exit_code = kExitDifferences;

    if (status.is_directory_entry) {
        if (ctx.global.format == OutputFormat::Json) {
            json arr = json::array();
            for (const auto& c : status.directory_changes) arr.push_back({{"path", c.relative_path}, {"change", to_string(c.change)}});
            json root;
            root["home_path"] = entry.home_path;
            root["changes"] = arr;
            result.output = root.dump(2) + "\n";
        } else {
            std::ostringstream out;
            out << "Directory differences for " << display_home_path(entry.home_path) << ":\n";
            for (const auto& c : status.directory_changes) out << "  " << to_string(c.change) << "  " << c.relative_path << "\n";
            result.output = out.str();
        }
        return result;
    }

    std::string herr, serr;
    auto home_content = read_file_to_string(status.home_abs, &herr);
    auto store_content = read_file_to_string(status.store_abs, &serr);
    if (!home_content || !store_content) {
        result.exit_code = kExitIoFailure;
        result.output = "error: failed to read file content for diff: " + (herr.empty() ? serr : herr) + "\n";
        return result;
    }
    if (looks_binary(*home_content) || looks_binary(*store_content)) {
        result.output = ctx.global.format == OutputFormat::Json
                             ? std::string("{\"binary\": true, \"message\": \"Binary files differ.\"}\n")
                             : std::string("Binary files differ.\n");
        return result;
    }

    auto a = split_lines(*store_content);
    auto b = split_lines(*home_content);
    auto d = diff_lines(a, b);
    if (!d.ok) {
        result.output = "(diff unavailable: " + d.skip_reason + ")\n";
        return result;
    }

    if (ctx.global.format == OutputFormat::Json) {
        json lines = json::array();
        for (const auto& l : d.lines) {
            std::string op = l.op == DiffOp::Context ? "context" : (l.op == DiffOp::Removed ? "removed" : "added");
            lines.push_back({{"op", op}, {"text", l.text}});
        }
        json root;
        root["home_path"] = entry.home_path;
        root["lines"] = lines;
        result.output = root.dump(2) + "\n";
    } else if (ctx.global.format == OutputFormat::Markdown) {
        std::ostringstream out;
        out << "### Diff: " << display_home_path(entry.home_path) << "\n\n```diff\n"
            << render_unified_diff(d, "store:" + entry.store_path, display_home_path(entry.home_path)) << "```\n";
        result.output = out.str();
    } else {
        result.output = render_unified_diff(d, "store:" + entry.store_path, display_home_path(entry.home_path));
    }
    return result;
}

namespace {

// Shared body for backup/apply/restore: runs `op` over the selected entries
// and renders a uniform report.
CommandResult run_sync_command(AppContext& ctx, const ParsedArgs& args, const std::string& verb,
                                const std::function<SyncResult(const ManagedEntry&)>& op) {
    CommandResult result;
    std::string err;
    auto selected = select_entries(ctx, args.positional, &err);
    // (false positive below: select_entries() returns an empty vector both
    // when there are simply zero managed entries -- err left empty -- and
    // when an explicit path argument didn't match a managed entry -- err
    // set. cppcheck's interprocedural analysis does not track the
    // std::string* out-parameter across the call, so both branches are
    // needed to tell these two cases apart at runtime.)
    // cppcheck-suppress knownConditionTrueFalse
    if (selected.empty() && !err.empty()) {
        result.exit_code = kExitUsageError;
        result.output = "error: " + err + "\n";
        return result;
    }

    std::vector<std::pair<const ManagedEntry*, SyncResult>> results;
    for (const auto* e : selected) results.emplace_back(e, op(*e));

    bool any_refused = false, any_failed = false, any_would_or_did_change = false;
    for (auto& [e, r] : results) {
        (void)e;
        if (is_refused(r.outcome)) {
            if (r.outcome == SyncOutcome::Failed)
                any_failed = true;
            else
                any_refused = true;
        }
        if (is_change(r.outcome) || r.outcome == SyncOutcome::WouldCreate || r.outcome == SyncOutcome::WouldUpdate ||
            r.outcome == SyncOutcome::WouldReplaceSymlink) {
            any_would_or_did_change = true;
        }
    }

    if (any_failed)
        result.exit_code = kExitIoFailure;
    else if (any_refused)
        result.exit_code = kExitUnresolvedConflict;
    else if (ctx.global.dry_run && any_would_or_did_change)
        result.exit_code = kExitDifferences;
    else
        result.exit_code = kExitSuccess;

    if (ctx.global.format == OutputFormat::Json) {
        json arr = json::array();
        for (auto& [e, r] : results) {
            json item;
            item["home_path"] = e->home_path;
            item["outcome"] = to_string(r.outcome);
            item["message"] = r.message;
            if (!r.changed_files.empty()) item["changed_files"] = r.changed_files;
            arr.push_back(std::move(item));
        }
        json root;
        root["operation"] = verb;
        root["dry_run"] = ctx.global.dry_run;
        root["results"] = arr;
        result.output = root.dump(2) + "\n";
        return result;
    }

    std::ostringstream out;
    out << (ctx.global.dry_run ? "DRY RUN: " : "") << verb << " (" << results.size() << " entr"
        << (results.size() == 1 ? "y" : "ies") << ")\n";
    if (ctx.global.format == OutputFormat::Markdown) {
        Table table;
        table.headers = {"Home Path", "Outcome", "Message"};
        for (auto& [e, r] : results) table.rows.push_back({display_home_path(e->home_path), to_string(r.outcome), r.message});
        std::ostringstream mdout;
        mdout << "# " << verb << "\n\n" << table.render_markdown();
        result.output = mdout.str();
        return result;
    }
    for (auto& [e, r] : results) {
        out << "  " << display_home_path(e->home_path) << "  [" << to_string(r.outcome) << "]  " << r.message << "\n";
    }
    result.output = out.str();
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// backup
// ---------------------------------------------------------------------------
CommandResult cmd_backup(AppContext& ctx, const ParsedArgs& args) {
    if (!ctx.global.dry_run) {
        std::string ensure_err;
        auto store_canon = ensure_directory_canonical(ctx.store_dir, true, &ensure_err);
        if (!store_canon) {
            CommandResult r;
            r.exit_code = kExitIoFailure;
            r.output = "error: failed to prepare store directory: " + ensure_err + "\n";
            return r;
        }
        ctx.store_dir = *store_canon;
        ctx.store_exists = true;
    }

    if (args.flags.count("snapshot") && ctx.store_exists) {
        std::string selectErr;
        auto selected = select_entries(ctx, args.positional, &selectErr);
        std::vector<ManagedEntry> to_snapshot;
        for (auto* e : selected) to_snapshot.push_back(*e);
        std::string snap_err;
        auto snap = create_snapshot(ctx.store_dir, to_snapshot, ctx.ignore, ctx.global.dry_run, &snap_err);
        if (!snap) {
            CommandResult r;
            r.exit_code = kExitIoFailure;
            r.output = "error: failed to create snapshot: " + snap_err + "\n";
            return r;
        }
    }

    return run_sync_command(ctx, args, "backup", [&](const ManagedEntry& e) {
        return sync_entry_home_to_store(ctx.home_dir, ctx.store_dir, e, ctx.ignore, ctx.global.dry_run,
                                         ctx.global.force);
    });
}

// ---------------------------------------------------------------------------
// apply
// ---------------------------------------------------------------------------
CommandResult cmd_apply(AppContext& ctx, const ParsedArgs& args) {
    if (!ctx.store_exists) {
        CommandResult r;
        r.exit_code = kExitDifferences;
        r.output = "Nothing to apply: the managed store does not exist yet.\n";
        return r;
    }
    return run_sync_command(ctx, args, "apply", [&](const ManagedEntry& e) {
        return sync_entry_store_to_home(ctx.home_dir, ctx.store_dir, e, ctx.ignore, ctx.global.dry_run,
                                         ctx.global.force);
    });
}

// ---------------------------------------------------------------------------
// restore
// ---------------------------------------------------------------------------
CommandResult cmd_restore(AppContext& ctx, const ParsedArgs& args) {
    if (!ctx.store_exists) {
        CommandResult r;
        r.exit_code = kExitUsageError;
        r.output = "error: the managed store does not exist yet; nothing to restore from\n";
        return r;
    }
    if (!args.options.count("snapshot-id")) {
        CommandResult r;
        r.exit_code = kExitUsageError;
        r.output = "error: 'restore' requires --snapshot-id ID (or --snapshot-id latest)\n";
        return r;
    }
    std::string snapshot_id = args.options.at("snapshot-id");
    if (snapshot_id == "latest") {
        auto snaps = list_snapshots(ctx.store_dir);
        if (snaps.empty()) {
            CommandResult r;
            r.exit_code = kExitUsageError;
            r.output = "error: no snapshots exist to restore from\n";
            return r;
        }
        snapshot_id = snaps.front().id;
    }

    return run_sync_command(ctx, args, "restore", [&](const ManagedEntry& e) {
        return restore_entry_from_snapshot(ctx.home_dir, ctx.store_dir, snapshot_id, e, ctx.ignore, ctx.global.dry_run,
                                            ctx.global.force);
    });
}

// ---------------------------------------------------------------------------
// check
// ---------------------------------------------------------------------------
CommandResult cmd_check(AppContext& ctx, const ParsedArgs& args) {
    CommandResult result;
    if (!args.positional.empty()) {
        result.exit_code = kExitUsageError;
        result.output = "error: 'check' does not take positional arguments\n";
        return result;
    }

    std::vector<std::string> problems;
    std::vector<std::string> warnings;

    auto config_issues = validate_config(ctx.config);
    for (const auto& issue : config_issues) {
        (issue.fatal ? problems : warnings).push_back(issue.message);
    }

    if (!ctx.store_exists) {
        warnings.push_back("store directory does not exist yet: " + ctx.config.store);
    }

    for (const auto& e : ctx.config.entries) {
        auto home_r = resolve_within(ctx.home_dir, e.home_path);
        if (!home_r.ok()) problems.push_back("entry '" + e.home_path + "': " + home_r.message);
        if (ctx.store_exists) {
            auto store_r = resolve_within(ctx.store_dir, e.store_path);
            if (!store_r.ok()) problems.push_back("entry '" + e.home_path + "': " + store_r.message);
        }
    }

    bool ok = problems.empty();
    if (ctx.global.format == OutputFormat::Json) {
        json root;
        root["ok"] = ok;
        root["problems"] = problems;
        root["warnings"] = warnings;
        result.output = root.dump(2) + "\n";
    } else if (ctx.global.format == OutputFormat::Markdown) {
        std::ostringstream out;
        out << "# Check\n\nStatus: " << (ok ? "OK" : "PROBLEMS FOUND") << "\n\n";
        if (!problems.empty()) {
            out << "## Problems\n";
            for (const auto& p : problems) out << "- " << p << "\n";
        }
        if (!warnings.empty()) {
            out << "\n## Warnings\n";
            for (const auto& w : warnings) out << "- " << w << "\n";
        }
        result.output = out.str();
    } else {
        std::ostringstream out;
        out << "Configuration check: " << (ok ? "OK" : "PROBLEMS FOUND") << "\n";
        out << "  config: " << ctx.config_path.string() << "\n";
        out << "  store:  " << ctx.config.store << (ctx.store_exists ? "" : " (does not exist yet)") << "\n";
        out << "  entries: " << ctx.config.entries.size() << "\n\n";
        if (!problems.empty()) {
            out << "Problems:\n";
            for (const auto& p : problems) out << "  - " << p << "\n";
        }
        if (!warnings.empty()) {
            out << "Warnings:\n";
            for (const auto& w : warnings) out << "  - " << w << "\n";
        }
        result.output = out.str();
    }

    result.exit_code = !problems.empty() ? kExitPathSafetyError : (!warnings.empty() ? kExitDifferences : kExitSuccess);
    return result;
}

// ---------------------------------------------------------------------------
// report
// ---------------------------------------------------------------------------
CommandResult cmd_report(AppContext& ctx, const ParsedArgs& args) {
    CommandResult result;
    if (!args.positional.empty()) {
        result.exit_code = kExitUsageError;
        result.output = "error: 'report' does not take positional arguments\n";
        return result;
    }

    ParsedArgs status_args = args;
    ParsedArgs check_args = args;
    auto status_result = cmd_status(ctx, status_args);
    auto check_result = cmd_check(ctx, check_args);
    auto snaps = ctx.store_exists ? list_snapshots(ctx.store_dir) : std::vector<SnapshotInfo>{};

    if (ctx.global.format == OutputFormat::Json) {
        json status_json = json::parse(status_result.output, nullptr, false);
        json check_json = json::parse(check_result.output, nullptr, false);
        json root;
        root["version"] = kVersion;
        root["store"] = ctx.config.store;
        root["managed_entries"] = ctx.config.entries.size();
        root["snapshot_count"] = snaps.size();
        root["status"] = status_json.is_discarded() ? json(nullptr) : status_json;
        root["check"] = check_json.is_discarded() ? json(nullptr) : check_json;
        result.output = root.dump(2) + "\n";
    } else if (ctx.global.format == OutputFormat::Markdown) {
        std::ostringstream out;
        out << "# Dotfile Manager Report\n\n";
        out << "- Version: " << kVersion << "\n";
        out << "- Store: `" << ctx.config.store << "`\n";
        out << "- Managed entries: " << ctx.config.entries.size() << "\n";
        out << "- Snapshots: " << snaps.size() << "\n\n";
        out << status_result.output << "\n" << check_result.output;
        result.output = out.str();
    } else {
        std::ostringstream out;
        out << "Dotfile Manager " << kVersion << " - Report\n";
        out << "==============================\n\n";
        out << "Snapshots: " << snaps.size() << "\n\n";
        out << status_result.output << "\n" << check_result.output;
        result.output = out.str();
    }

    result.exit_code = std::max(status_result.exit_code, check_result.exit_code);
    return result;
}

}  // namespace dfm
