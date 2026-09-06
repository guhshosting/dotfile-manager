#include "dfm/config.hpp"

#include "dfm/atomic_io.hpp"
#include "dfm/env.hpp"
#include "dfm/path_safety.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace dfm {

namespace fs = std::filesystem;
using nlohmann::json;

std::string to_string(ManageMode mode) { return mode == ManageMode::Copy ? "copy" : "symlink"; }

std::optional<ManageMode> manage_mode_from_string(const std::string& s) {
    if (s == "copy") return ManageMode::Copy;
    if (s == "symlink") return ManageMode::Symlink;
    return std::nullopt;
}

const ManagedEntry* Config::find_by_home_path(const std::string& home_path) const {
    auto it = std::find_if(entries.begin(), entries.end(),
                            [&](const ManagedEntry& e) { return e.home_path == home_path; });
    return it == entries.end() ? nullptr : &(*it);
}

ManagedEntry* Config::find_by_home_path(const std::string& home_path) {
    auto it = std::find_if(entries.begin(), entries.end(),
                            [&](const ManagedEntry& e) { return e.home_path == home_path; });
    return it == entries.end() ? nullptr : &(*it);
}

std::vector<ConfigValidationIssue> validate_config(const Config& config) {
    std::vector<ConfigValidationIssue> issues;

    if (config.store.empty()) {
        issues.push_back({true, "config 'store' must not be empty"});
    } else if (!fs::path(config.store).is_absolute()) {
        issues.push_back({true, "config 'store' must be an absolute path: " + config.store});
    }

    std::set<std::string> seen_home;
    std::set<std::string> seen_store;

    for (const auto& e : config.entries) {
        auto home_check = validate_relative_path(e.home_path);
        if (!home_check.ok()) {
            issues.push_back({true, "entry home_path '" + e.home_path + "' is invalid: " + home_check.message});
        }
        auto store_check = validate_relative_path(e.store_path);
        if (!store_check.ok()) {
            issues.push_back({true, "entry store_path '" + e.store_path + "' is invalid: " + store_check.message});
        }
        if (!seen_home.insert(e.home_path).second) {
            issues.push_back({true, "duplicate managed entry for home_path '" + e.home_path + "'"});
        }
        if (!seen_store.insert(e.store_path).second) {
            issues.push_back({true, "duplicate managed entry for store_path '" + e.store_path + "'"});
        }
    }

    // Non-fatal advisory: nested managed entries (one entry's home_path is an
    // ancestor directory of another's). This is allowed but worth surfacing.
    for (std::size_t i = 0; i < config.entries.size(); ++i) {
        for (std::size_t j = 0; j < config.entries.size(); ++j) {
            if (i == j) continue;
            const fs::path a(config.entries[i].home_path);
            const fs::path b(config.entries[j].home_path);
            if (a == b) continue;
            auto rel = fs::path(b).lexically_relative(a);
            if (!rel.empty() && rel.string().rfind("..", 0) != 0 && rel.string() != ".") {
                issues.push_back({false, "entry '" + config.entries[j].home_path +
                                              "' is nested under managed entry '" +
                                              config.entries[i].home_path + "'"});
            }
        }
    }

    return issues;
}

bool is_config_usable(const Config& config) {
    auto issues = validate_config(config);
    return std::none_of(issues.begin(), issues.end(), [](const ConfigValidationIssue& i) { return i.fatal; });
}

ConfigLoadResult parse_config(const std::string& json_text) {
    ConfigLoadResult result;
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::parse_error& e) {
        result.ok = false;
        result.error = std::string("malformed JSON: ") + e.what();
        return result;
    }

    if (!root.is_object()) {
        result.ok = false;
        result.error = "config root must be a JSON object";
        return result;
    }

    Config cfg;
    if (root.contains("store")) {
        if (!root["store"].is_string()) {
            result.ok = false;
            result.error = "config field 'store' must be a string";
            return result;
        }
        cfg.store = root["store"].get<std::string>();
    }

    if (root.contains("use_default_ignores")) {
        if (!root["use_default_ignores"].is_boolean()) {
            result.ok = false;
            result.error = "config field 'use_default_ignores' must be a boolean";
            return result;
        }
        cfg.use_default_ignores = root["use_default_ignores"].get<bool>();
    }

    if (root.contains("ignore_patterns")) {
        if (!root["ignore_patterns"].is_array()) {
            result.ok = false;
            result.error = "config field 'ignore_patterns' must be an array of strings";
            return result;
        }
        for (const auto& v : root["ignore_patterns"]) {
            if (!v.is_string()) {
                result.ok = false;
                result.error = "config field 'ignore_patterns' must contain only strings";
                return result;
            }
            cfg.ignore_patterns.push_back(v.get<std::string>());
        }
    }

    if (root.contains("entries")) {
        if (!root["entries"].is_array()) {
            result.ok = false;
            result.error = "config field 'entries' must be an array";
            return result;
        }
        for (const auto& item : root["entries"]) {
            if (!item.is_object()) {
                result.ok = false;
                result.error = "each entry must be a JSON object";
                return result;
            }
            ManagedEntry entry;
            if (!item.contains("home_path") || !item["home_path"].is_string()) {
                result.ok = false;
                result.error = "entry missing required string field 'home_path'";
                return result;
            }
            entry.home_path = item["home_path"].get<std::string>();

            if (!item.contains("store_path") || !item["store_path"].is_string()) {
                result.ok = false;
                result.error = "entry '" + entry.home_path + "' missing required string field 'store_path'";
                return result;
            }
            entry.store_path = item["store_path"].get<std::string>();

            std::string mode_str = "copy";
            if (item.contains("mode")) {
                if (!item["mode"].is_string()) {
                    result.ok = false;
                    result.error = "entry '" + entry.home_path + "' field 'mode' must be a string";
                    return result;
                }
                mode_str = item["mode"].get<std::string>();
            }
            auto mode = manage_mode_from_string(mode_str);
            if (!mode) {
                result.ok = false;
                result.error =
                    "entry '" + entry.home_path + "' has invalid mode '" + mode_str + "' (expected copy|symlink)";
                return result;
            }
            entry.mode = *mode;

            if (item.contains("tags")) {
                if (!item["tags"].is_array()) {
                    result.ok = false;
                    result.error = "entry '" + entry.home_path + "' field 'tags' must be an array of strings";
                    return result;
                }
                for (const auto& t : item["tags"]) {
                    if (!t.is_string()) {
                        result.ok = false;
                        result.error = "entry '" + entry.home_path + "' field 'tags' must contain only strings";
                        return result;
                    }
                    entry.tags.push_back(t.get<std::string>());
                }
            }

            if (item.contains("ignore")) {
                if (!item["ignore"].is_array()) {
                    result.ok = false;
                    result.error = "entry '" + entry.home_path + "' field 'ignore' must be an array of strings";
                    return result;
                }
                for (const auto& t : item["ignore"]) {
                    if (!t.is_string()) {
                        result.ok = false;
                        result.error = "entry '" + entry.home_path + "' field 'ignore' must contain only strings";
                        return result;
                    }
                    entry.ignore.push_back(t.get<std::string>());
                }
            }

            cfg.entries.push_back(std::move(entry));
        }
    }

    result.ok = true;
    result.config = std::move(cfg);
    return result;
}

std::string serialize_config(const Config& config) {
    json root = json::object();
    root["store"] = config.store;
    root["use_default_ignores"] = config.use_default_ignores;
    root["ignore_patterns"] = config.ignore_patterns;

    json entries = json::array();
    for (const auto& e : config.entries) {
        json item = json::object();
        item["home_path"] = e.home_path;
        item["store_path"] = e.store_path;
        item["mode"] = to_string(e.mode);
        item["tags"] = e.tags;
        item["ignore"] = e.ignore;
        entries.push_back(std::move(item));
    }
    root["entries"] = std::move(entries);
    root["version"] = 1;

    return root.dump(2) + "\n";
}

ConfigLoadResult load_config_file(const fs::path& path) {
    ConfigLoadResult result;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        result.ok = true;
        result.config = Config{};
        return result;
    }

    std::string err;
    auto content = read_file_to_string(path, &err);
    if (!content) {
        result.ok = false;
        result.error = err;
        return result;
    }

    return parse_config(*content);
}

std::optional<std::string> save_config_file(const fs::path& path, const Config& config) {
    return atomic_write_file(path, serialize_config(config), fs::perms::owner_read | fs::perms::owner_write);
}

fs::path default_config_dir() { return xdg_config_home() / "dotfile-manager"; }
fs::path default_config_path() { return default_config_dir() / "config.json"; }
fs::path default_state_dir() { return xdg_state_home() / "dotfile-manager"; }

fs::path default_store_dir() {
    if (auto h = home_dir()) return *h / "dotfiles";
    return fs::path("dotfiles");
}

}  // namespace dfm
