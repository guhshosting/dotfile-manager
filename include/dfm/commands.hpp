// commands.hpp - subcommand implementations.
// SPDX-License-Identifier: MIT
#pragma once

#include "dfm/cli.hpp"
#include "dfm/exit_codes.hpp"

#include <string>

namespace dfm {

struct CommandResult {
    int exit_code = kExitSuccess;
    std::string output;  // fully rendered text ready to print (respects --format)
};

CommandResult cmd_discover(AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_list(const AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_add(AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_remove(AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_status(AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_diff(AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_backup(AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_apply(AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_restore(AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_check(AppContext& ctx, const ParsedArgs& args);
CommandResult cmd_report(AppContext& ctx, const ParsedArgs& args);

// Resolves a user-supplied CLI path argument (absolute, "~/..."-prefixed, or
// already relative-to-home) into a HOME-relative, '/'-separated string
// suitable for use as ManagedEntry::home_path. Returns std::nullopt (with
// *error set) if the path cannot be safely resolved within HOME.
std::optional<std::string> resolve_user_path_to_home_relative(const std::filesystem::path& home_dir,
                                                                const std::string& user_path, std::string* error);

}  // namespace dfm
