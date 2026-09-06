// env.hpp - environment / XDG helpers.
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace dfm {

// Returns $HOME, validated to be a non-empty absolute path. Returns
// std::nullopt if HOME is unset, empty, or not absolute (the caller must
// treat this as a fatal configuration error - dotfile-manager refuses to
// guess at a home directory).
std::optional<std::filesystem::path> home_dir();

// $XDG_CONFIG_HOME or $HOME/.config
std::filesystem::path xdg_config_home();

// $XDG_STATE_HOME or $HOME/.local/state
std::filesystem::path xdg_state_home();

// True if the effective user id is 0 (root).
bool running_as_root();

// Returns the raw environment variable value, or std::nullopt if unset or
// empty.
std::optional<std::string> get_env(const char* name);

}  // namespace dfm
