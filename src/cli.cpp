#include "dfm/cli.hpp"

#include "dfm/commands.hpp"
#include "dfm/env.hpp"
#include "dfm/exit_codes.hpp"
#include "dfm/path_safety.hpp"
#include "dfm/version.hpp"

#include <fstream>
#include <iostream>

namespace dfm {

namespace fs = std::filesystem;

const std::vector<std::string>& known_commands() {
    static const std::vector<std::string> cmds = {"discover", "list",   "add",   "remove", "status", "diff",
                                                    "backup",  "apply", "restore", "check",  "report", "version"};
    return cmds;
}

bool command_accepts_force(const std::string& command) {
    return command == "add" || command == "backup" || command == "apply" || command == "restore";
}

bool command_accepts_dry_run(const std::string& command) {
    return command == "add" || command == "remove" || command == "backup" || command == "apply" ||
           command == "restore";
}

std::string help_text() {
    return R"(dotfile-manager 1.0.0 - safe CLI dotfile manager

USAGE:
  dotfile-manager <command> [arguments] [options]

COMMANDS:
  discover                Find likely dotfiles/config files (read-only)
  list [--snapshots]      List currently managed entries, or snapshots
  add <path> [opts]       Add a path to the managed set (copies into the store)
  remove <path> [opts]    Remove a path from management metadata
  status [paths...]       Show sync status of managed entries
  diff <path>             Show differences between store and home versions
  backup [paths...]       Copy home content into the managed store
  apply [paths...]        Apply managed store content to home
  restore --snapshot-id ID [paths...]
                          Restore home content from a snapshot
  check                   Validate configuration and managed paths
  report                  Produce a complete status/check report
  version                 Print version information

GLOBAL OPTIONS:
  --format text|json|markdown   Output format (default: text)
  --output FILE                 Write output to FILE instead of stdout
  --verbose                     Include additional detail
  --quiet                       Suppress non-essential messages
  --no-color                    Disable ANSI colors
  --dry-run                     Preview a mutating operation without changing anything
  --force                       Allow an explicitly-documented destructive operation
  --help, -h                    Show this help text
  --version                     Show version information

ADD OPTIONS:
  --mode copy|symlink           Management mode for this entry (default: copy)
  --store-path PATH             Override the relative path used inside the store
  --tag NAME                    Attach a tag (may be repeated)
  --allow-sensitive             Required to add a path classified as sensitive

REMOVE OPTIONS:
  --purge-store                 Also delete the store copy (never recursive; refuses on non-empty directories)

BACKUP OPTIONS:
  --snapshot                    Create a timestamped snapshot before syncing

RESTORE OPTIONS:
  --snapshot-id ID|latest       Which snapshot to restore from (required)

EXIT CODES:
  0 success / everything in sync   1 differences or warnings   2 usage error
  3 configuration error            4 path safety error         5 unresolved conflict
  6 I/O failure                    7 internal error

See README.md and man dotfile-manager for full documentation.
)";
}

namespace {

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

ArgParseResult parse_args(int argc, char** argv) {
    ArgParseResult result;
    std::vector<std::string> tokens;
    for (int i = 1; i < argc; ++i) tokens.emplace_back(argv[i]);

    if (tokens.empty()) {
        result.ok = false;
        result.error = "no command given";
        result.show_help = true;
        return result;
    }

    if (tokens[0] == "--help" || tokens[0] == "-h") {
        result.ok = true;
        result.show_help = true;
        return result;
    }
    if (tokens[0] == "--version") {
        result.ok = true;
        result.show_version = true;
        return result;
    }

    const std::string& command = tokens[0];
    bool known = false;
    for (const auto& c : known_commands()) {
        if (c == command) {
            known = true;
            break;
        }
    }
    if (!known) {
        result.ok = false;
        result.error = "unknown command '" + command + "'";
        return result;
    }
    result.args.command = command;

    auto take_value = [&](std::size_t& i, const std::string& opt_name, std::string& out) -> bool {
        auto eq = tokens[i].find('=');
        if (eq != std::string::npos) {
            out = tokens[i].substr(eq + 1);
            return true;
        }
        if (i + 1 >= tokens.size()) {
            result.error = "option '" + opt_name + "' requires a value";
            return false;
        }
        out = tokens[++i];
        return true;
    };

    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string& tok = tokens[i];
        std::string opt = tok;
        auto eq_pos = tok.find('=');
        if (eq_pos != std::string::npos) opt = tok.substr(0, eq_pos);

        if (opt == "--help" || tok == "-h") {
            result.ok = true;
            result.show_help = true;
            return result;
        }
        if (opt == "--format") {
            std::string v;
            if (!take_value(i, opt, v)) return result;
            auto fmt = parse_output_format(v);
            if (!fmt) {
                result.error = "invalid --format value '" + v + "' (expected text|json|markdown)";
                return result;
            }
            result.args.global.format = *fmt;
        } else if (opt == "--output") {
            std::string v;
            if (!take_value(i, opt, v)) return result;
            result.args.global.output_file = fs::path(v);
        } else if (opt == "--verbose") {
            result.args.global.verbose = true;
        } else if (opt == "--quiet") {
            result.args.global.quiet = true;
        } else if (opt == "--no-color") {
            result.args.global.no_color = true;
        } else if (opt == "--dry-run") {
            if (!command_accepts_dry_run(command)) {
                result.error = "the '" + command + "' command does not accept --dry-run";
                return result;
            }
            result.args.global.dry_run = true;
        } else if (opt == "--force") {
            if (!command_accepts_force(command)) {
                result.error = "the '" + command + "' command does not accept --force";
                return result;
            }
            result.args.global.force = true;
        } else if (opt == "--mode" && command == "add") {
            std::string v;
            if (!take_value(i, opt, v)) return result;
            result.args.options["mode"] = v;
        } else if (opt == "--store-path" && command == "add") {
            std::string v;
            if (!take_value(i, opt, v)) return result;
            result.args.options["store-path"] = v;
        } else if (opt == "--tag" && command == "add") {
            std::string v;
            if (!take_value(i, opt, v)) return result;
            if (!result.args.options["tags"].empty()) result.args.options["tags"] += ",";
            result.args.options["tags"] += v;
        } else if (opt == "--allow-sensitive" && command == "add") {
            result.args.flags.insert("allow-sensitive");
        } else if (opt == "--purge-store" && command == "remove") {
            result.args.flags.insert("purge-store");
        } else if (opt == "--snapshot" && command == "backup") {
            result.args.flags.insert("snapshot");
        } else if (opt == "--snapshot-id" && command == "restore") {
            std::string v;
            if (!take_value(i, opt, v)) return result;
            result.args.options["snapshot-id"] = v;
        } else if (opt == "--snapshots" && command == "list") {
            result.args.flags.insert("snapshots");
        } else if (starts_with(opt, "--")) {
            result.error = "unknown option '" + opt + "' for command '" + command + "'";
            return result;
        } else if (starts_with(tok, "-") && tok != "-") {
            result.error = "unknown option '" + tok + "' for command '" + command + "'";
            return result;
        } else {
            result.args.positional.push_back(tok);
        }
    }

    result.ok = true;
    return result;
}

namespace {

std::filesystem::path expand_user_home_shortcut(const std::string& p, const fs::path& home) {
    if (p == "~") return home;
    if (starts_with(p, "~/")) return home / p.substr(2);
    return fs::path(p);
}

}  // namespace

int dispatch(const ParsedArgs& args, std::ostream& out, std::ostream& err) {
    if (args.command == "version") {
        out << version_line_full();
        return kExitSuccess;
    }

    auto home = home_dir();
    if (!home) {
        err << "error: $HOME is not set to a valid absolute path; refusing to guess a home directory\n";
        return kExitConfigError;
    }
    if (running_as_root()) {
        err << "warning: running as root - dotfile-manager will manage root's dotfiles, which have different "
               "consequences than a normal user's; proceed with care\n";
    }

    AppContext ctx;
    ctx.home_dir = *home;
    ctx.global = args.global;
    ctx.out = &out;
    ctx.config_path = default_config_path();

    auto load = load_config_file(ctx.config_path);
    if (!load.ok) {
        err << "error: configuration is malformed and was left untouched: " << ctx.config_path.string() << "\n";
        err << "  " << load.error << "\n";
        err << "Fix or remove the file manually, then retry.\n";
        return kExitConfigError;
    }
    ctx.config = load.config;
    if (ctx.config.store.empty()) {
        ctx.config.store = default_store_dir().string();
    }

    auto issues = validate_config(ctx.config);
    for (const auto& issue : issues) {
        if (issue.fatal) {
            err << "error: configuration error: " << issue.message << "\n";
            return kExitConfigError;
        }
    }
    if (args.global.verbose) {
        for (const auto& issue : issues) {
            if (!issue.fatal) err << "note: " << issue.message << "\n";
        }
    }

    ctx.ignore = IgnoreRules(ctx.config.ignore_patterns);
    if (ctx.config.use_default_ignores) {
        for (const auto& p : IgnoreRules::default_patterns()) ctx.ignore.add_pattern(p);
    }

    // Resolve/canonicalize the store directory. Commands that only read
    // (discover/list/status/diff/check/report) tolerate a missing store;
    // mutating commands create it on demand.
    fs::path configured_store(ctx.config.store);
    auto existing_store = try_canonical(configured_store);
    if (existing_store) {
        ctx.store_dir = *existing_store;
        ctx.store_exists = true;
    } else {
        ctx.store_dir = configured_store;  // not yet canonical; created lazily by mutating commands
        ctx.store_exists = false;
    }

    CommandResult result;
    if (args.command == "discover") {
        result = cmd_discover(ctx, args);
    } else if (args.command == "list") {
        result = cmd_list(ctx, args);
    } else if (args.command == "add") {
        result = cmd_add(ctx, args);
    } else if (args.command == "remove") {
        result = cmd_remove(ctx, args);
    } else if (args.command == "status") {
        result = cmd_status(ctx, args);
    } else if (args.command == "diff") {
        result = cmd_diff(ctx, args);
    } else if (args.command == "backup") {
        result = cmd_backup(ctx, args);
    } else if (args.command == "apply") {
        result = cmd_apply(ctx, args);
    } else if (args.command == "restore") {
        result = cmd_restore(ctx, args);
    } else if (args.command == "check") {
        result = cmd_check(ctx, args);
    } else if (args.command == "report") {
        result = cmd_report(ctx, args);
    } else {
        err << "internal error: unhandled command '" << args.command << "'\n";
        return kExitInternalError;
    }

    if (args.global.output_file) {
        std::ofstream f(*args.global.output_file, std::ios::binary | std::ios::trunc);
        if (!f) {
            err << "error: failed to open --output file for writing: " << args.global.output_file->string() << "\n";
            return kExitIoFailure;
        }
        f << result.output;
    } else {
        out << result.output;
    }

    return result.exit_code;
}

std::optional<std::string> resolve_user_path_to_home_relative(const fs::path& home_dir, const std::string& user_path,
                                                                std::string* error) {
    fs::path expanded = expand_user_home_shortcut(user_path, home_dir);
    fs::path absolute = expanded.is_absolute() ? expanded : fs::absolute(expanded);
    auto weak = try_weakly_canonical(absolute);
    fs::path effective = weak.value_or(absolute.lexically_normal());

    auto home_canon = try_canonical(home_dir);
    if (!home_canon) {
        if (error) *error = "home directory could not be canonicalized: " + home_dir.string();
        return std::nullopt;
    }

    auto rel = effective.lexically_relative(*home_canon);
    std::string rel_str = rel.generic_string();
    if (rel.empty() || rel_str == "." || starts_with(rel_str, "../") || rel_str == "..") {
        if (error) *error = "path is not inside the home directory: " + user_path;
        return std::nullopt;
    }

    auto check = validate_relative_path(rel_str);
    if (!check.ok()) {
        if (error) *error = "path is unsafe: " + check.message;
        return std::nullopt;
    }
    return check.resolved.generic_string();
}

int run_cli(int argc, char** argv) {
    auto parsed = parse_args(argc, argv);
    if (parsed.show_help) {
        std::cout << help_text();
        return parsed.ok ? kExitSuccess : kExitUsageError;
    }
    if (parsed.show_version) {
        std::cout << version_line_full();
        return kExitSuccess;
    }
    if (!parsed.ok) {
        std::cerr << "error: " << parsed.error << "\n\n" << help_text();
        return kExitUsageError;
    }
    return dispatch(parsed.args, std::cout, std::cerr);
}

}  // namespace dfm
