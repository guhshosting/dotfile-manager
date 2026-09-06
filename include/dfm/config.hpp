// config.hpp - persisted configuration model (XDG config.json).
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dfm {

enum class ManageMode { Copy, Symlink };

std::string to_string(ManageMode mode);
std::optional<ManageMode> manage_mode_from_string(const std::string& s);

struct ManagedEntry {
    std::string home_path;                  // relative to HOME, e.g. ".bashrc"
    std::string store_path;                 // relative to store, e.g. "bash/.bashrc"
    ManageMode mode = ManageMode::Copy;
    std::vector<std::string> tags;
    std::vector<std::string> ignore;        // extra per-entry ignore patterns (directories)
};

struct Config {
    std::string store;                          // absolute path to the managed store
    std::vector<ManagedEntry> entries;
    std::vector<std::string> ignore_patterns;   // extra global ignore patterns
    bool use_default_ignores = true;            // append IgnoreRules::default_patterns()

    const ManagedEntry* find_by_home_path(const std::string& home_path) const;
    ManagedEntry* find_by_home_path(const std::string& home_path);
};

struct ConfigLoadResult {
    bool ok = false;
    Config config;
    std::string error;  // set when !ok; original file is left untouched
};

struct ConfigValidationIssue {
    bool fatal = false;   // fatal issues make the config unusable
    std::string message;
};

// Validates structural + safety invariants of a config (relative-path
// safety, mode validity, duplicate entries, store non-empty). Does not
// touch the filesystem. Returns an empty vector when the config is fully
// valid; otherwise returns all discovered issues (some may be non-fatal,
// e.g. nested-entry advisories).
std::vector<ConfigValidationIssue> validate_config(const Config& config);

// Returns true if `validate_config` reports no fatal issues.
bool is_config_usable(const Config& config);

// Parses JSON text into a Config. Does NOT validate path safety semantics
// beyond basic structural sanity (use validate_config for that). On
// malformed JSON or a structurally wrong schema, ok=false and error is a
// human-readable explanation; the caller must not treat config as usable.
ConfigLoadResult parse_config(const std::string& json_text);

// Serializes a Config to pretty-printed, stable-ordered JSON text.
std::string serialize_config(const Config& config);

// Loads configuration from `path`. If the file does not exist, returns
// ok=true with a fresh default-constructed Config (store left empty; caller
// should apply default_store_path()). If the file exists but is malformed,
// returns ok=false and NEVER modifies the file.
ConfigLoadResult load_config_file(const std::filesystem::path& path);

// Atomically writes `config` to `path` (temp file + fsync + rename).
// Creates parent directories as needed. Returns an error message on
// failure, or std::nullopt on success.
std::optional<std::string> save_config_file(const std::filesystem::path& path, const Config& config);

// XDG-aware default paths.
std::filesystem::path default_config_dir();   // $XDG_CONFIG_HOME/dotfile-manager or ~/.config/dotfile-manager
std::filesystem::path default_config_path();  // default_config_dir()/config.json
std::filesystem::path default_state_dir();    // $XDG_STATE_HOME/dotfile-manager or ~/.local/state/dotfile-manager
std::filesystem::path default_store_dir();    // $HOME/dotfiles

}  // namespace dfm
