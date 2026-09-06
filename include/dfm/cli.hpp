// cli.hpp - argument parsing and top-level dispatch.
// SPDX-License-Identifier: MIT
#pragma once

#include "dfm/config.hpp"
#include "dfm/ignore_rules.hpp"
#include "dfm/output.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace dfm {

struct GlobalOptions {
    OutputFormat format = OutputFormat::Text;
    std::optional<std::filesystem::path> output_file;
    bool verbose = false;
    bool quiet = false;
    bool no_color = false;
    bool dry_run = false;
    bool force = false;
};

struct ParsedArgs {
    std::string command;
    std::vector<std::string> positional;
    GlobalOptions global;
    std::map<std::string, std::string> options;  // e.g. "mode" -> "symlink", "store-path" -> "..."
    std::set<std::string> flags;                 // e.g. "allow-sensitive", "snapshot", "purge-store"
};

struct ArgParseResult {
    bool ok = false;
    ParsedArgs args;
    std::string error;
    bool show_help = false;
    bool show_version = false;
};

// Set of commands recognized by the CLI (used for help text and validation).
const std::vector<std::string>& known_commands();

// True if `command` accepts --force (used to reject --force for commands
// where it is meaningless, per the CLI contract).
bool command_accepts_force(const std::string& command);
bool command_accepts_dry_run(const std::string& command);

ArgParseResult parse_args(int argc, char** argv);

std::string help_text();

struct AppContext {
    std::filesystem::path home_dir;
    std::filesystem::path config_path;
    std::filesystem::path store_dir;  // canonical; may not exist yet for a brand-new setup
    bool store_exists = false;
    Config config;
    IgnoreRules ignore;
    GlobalOptions global;
    std::ostream* out = nullptr;
};

// Runs the full CLI given already-parsed arguments. Returns the process
// exit code (see exit_codes.hpp).
int dispatch(const ParsedArgs& args, std::ostream& out, std::ostream& err);

// Entry point used by main().
int run_cli(int argc, char** argv);

}  // namespace dfm
