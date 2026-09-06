// test_config.cpp - unit tests for dfm/config.hpp.
// SPDX-License-Identifier: MIT
#include "dfm/config.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

using namespace dfm;
using namespace dfm_test;

TEST_CASE("manage_mode_from_string parses copy and symlink") {
    CHECK(manage_mode_from_string("copy") == ManageMode::Copy);
    CHECK(manage_mode_from_string("symlink") == ManageMode::Symlink);
}

TEST_CASE("manage_mode_from_string rejects unknown values") {
    CHECK_FALSE(manage_mode_from_string("hardlink").has_value());
    CHECK_FALSE(manage_mode_from_string("").has_value());
    CHECK_FALSE(manage_mode_from_string("Copy").has_value());
}

TEST_CASE("to_string(ManageMode) round-trips through manage_mode_from_string") {
    CHECK(manage_mode_from_string(to_string(ManageMode::Copy)) == ManageMode::Copy);
    CHECK(manage_mode_from_string(to_string(ManageMode::Symlink)) == ManageMode::Symlink);
}

TEST_CASE("Config::find_by_home_path finds an existing entry") {
    Config cfg;
    cfg.entries.push_back({".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}});
    auto* found = cfg.find_by_home_path(".bashrc");
    REQUIRE(found != nullptr);
    CHECK(found->store_path == "bash/.bashrc");
}

TEST_CASE("Config::find_by_home_path returns nullptr when missing") {
    Config cfg;
    CHECK(cfg.find_by_home_path(".vimrc") == nullptr);
}

TEST_CASE("Config::find_by_home_path const overload also works") {
    Config cfg;
    cfg.entries.push_back({".vimrc", "vim/.vimrc", ManageMode::Symlink, {}, {}});
    const Config& cref = cfg;
    CHECK(cref.find_by_home_path(".vimrc") != nullptr);
}

// ---------------------------------------------------------------------------
// validate_config
// ---------------------------------------------------------------------------

TEST_CASE("validate_config reports a fatal issue for an empty store") {
    Config cfg;
    cfg.store = "";
    auto issues = validate_config(cfg);
    bool found = false;
    for (auto& i : issues)
        if (i.fatal) found = true;
    CHECK(found);
    CHECK_FALSE(is_config_usable(cfg));
}

TEST_CASE("validate_config reports a fatal issue for a relative store path") {
    Config cfg;
    cfg.store = "relative/dotfiles";
    CHECK_FALSE(is_config_usable(cfg));
}

TEST_CASE("validate_config accepts a well-formed config with one entry") {
    Config cfg;
    cfg.store = "/home/user/dotfiles";
    cfg.entries.push_back({".bashrc", "bash/.bashrc", ManageMode::Copy, {"shell"}, {}});
    CHECK(is_config_usable(cfg));
}

TEST_CASE("validate_config rejects an entry with a traversal home_path") {
    Config cfg;
    cfg.store = "/home/user/dotfiles";
    cfg.entries.push_back({"../../etc/passwd", "x", ManageMode::Copy, {}, {}});
    CHECK_FALSE(is_config_usable(cfg));
}

TEST_CASE("validate_config rejects an entry with a traversal store_path") {
    Config cfg;
    cfg.store = "/home/user/dotfiles";
    cfg.entries.push_back({".bashrc", "../../etc/evil", ManageMode::Copy, {}, {}});
    CHECK_FALSE(is_config_usable(cfg));
}

TEST_CASE("validate_config rejects duplicate home_path entries") {
    Config cfg;
    cfg.store = "/home/user/dotfiles";
    cfg.entries.push_back({".bashrc", "a", ManageMode::Copy, {}, {}});
    cfg.entries.push_back({".bashrc", "b", ManageMode::Copy, {}, {}});
    CHECK_FALSE(is_config_usable(cfg));
}

TEST_CASE("validate_config rejects duplicate store_path entries") {
    Config cfg;
    cfg.store = "/home/user/dotfiles";
    cfg.entries.push_back({".bashrc", "shared", ManageMode::Copy, {}, {}});
    cfg.entries.push_back({".vimrc", "shared", ManageMode::Copy, {}, {}});
    CHECK_FALSE(is_config_usable(cfg));
}

TEST_CASE("validate_config reports nested entries as a non-fatal advisory") {
    Config cfg;
    cfg.store = "/home/user/dotfiles";
    cfg.entries.push_back({".config", "config", ManageMode::Copy, {}, {}});
    cfg.entries.push_back({".config/nvim", "config/nvim", ManageMode::Copy, {}, {}});
    auto issues = validate_config(cfg);
    bool has_nested_advisory = false;
    bool has_fatal = false;
    for (auto& i : issues) {
        if (!i.fatal) has_nested_advisory = true;
        if (i.fatal) has_fatal = true;
    }
    CHECK(has_nested_advisory);
    CHECK_FALSE(has_fatal);
    CHECK(is_config_usable(cfg));  // nested is a warning, not fatal
}

// ---------------------------------------------------------------------------
// parse_config / serialize_config
// ---------------------------------------------------------------------------

TEST_CASE("parse_config parses a minimal valid document") {
    auto r = parse_config(R"({"store": "/home/user/dotfiles", "entries": []})");
    REQUIRE(r.ok);
    CHECK(r.config.store == "/home/user/dotfiles");
    CHECK(r.config.entries.empty());
}

TEST_CASE("parse_config parses the documented example schema") {
    auto r = parse_config(R"({
        "store": "/home/user/dotfiles",
        "entries": [
            {"home_path": ".bashrc", "store_path": "bash/.bashrc", "mode": "copy", "tags": ["shell"]}
        ]
    })");
    REQUIRE(r.ok);
    REQUIRE(r.config.entries.size() == 1);
    CHECK(r.config.entries[0].home_path == ".bashrc");
    CHECK(r.config.entries[0].store_path == "bash/.bashrc");
    CHECK(r.config.entries[0].mode == ManageMode::Copy);
    CHECK(r.config.entries[0].tags == std::vector<std::string>{"shell"});
}

TEST_CASE("parse_config rejects malformed JSON without throwing") {
    auto r = parse_config("{not valid json");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("parse_config rejects a non-object root") {
    auto r = parse_config("[1, 2, 3]");
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_config rejects an entry missing home_path") {
    auto r = parse_config(R"({"entries": [{"store_path": "x"}]})");
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_config rejects an entry missing store_path") {
    auto r = parse_config(R"({"entries": [{"home_path": ".bashrc"}]})");
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_config rejects an entry with an invalid mode value") {
    auto r = parse_config(R"({"entries": [{"home_path": ".bashrc", "store_path": "x", "mode": "hardlink"}]})");
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_config rejects a non-array 'entries' field") {
    auto r = parse_config(R"({"entries": "not-an-array"})");
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_config rejects a non-string 'store' field") {
    auto r = parse_config(R"({"store": 123})");
    CHECK_FALSE(r.ok);
}

TEST_CASE("parse_config rejects tags that are not strings") {
    auto r = parse_config(R"({"entries": [{"home_path": "a", "store_path": "b", "tags": [1, 2]}]})");
    CHECK_FALSE(r.ok);
}

TEST_CASE("serialize_config then parse_config round-trips a config") {
    Config cfg;
    cfg.store = "/home/user/dotfiles";
    cfg.use_default_ignores = false;
    cfg.ignore_patterns = {"*.bak"};
    cfg.entries.push_back({".bashrc", "bash/.bashrc", ManageMode::Copy, {"shell", "posix"}, {"local/"}});
    cfg.entries.push_back({".vimrc", "vim/.vimrc", ManageMode::Symlink, {}, {}});

    std::string text = serialize_config(cfg);
    auto r = parse_config(text);
    REQUIRE(r.ok);
    CHECK(r.config.store == cfg.store);
    CHECK(r.config.use_default_ignores == false);
    CHECK(r.config.ignore_patterns == cfg.ignore_patterns);
    REQUIRE(r.config.entries.size() == 2);
    CHECK(r.config.entries[0].home_path == ".bashrc");
    CHECK(r.config.entries[0].tags.size() == 2);
    CHECK(r.config.entries[0].ignore == std::vector<std::string>{"local/"});
    CHECK(r.config.entries[1].mode == ManageMode::Symlink);
}

// ---------------------------------------------------------------------------
// load_config_file / save_config_file
// ---------------------------------------------------------------------------

TEST_CASE("load_config_file returns a fresh default config when the file does not exist") {
    TempDir tmp;
    auto r = load_config_file(tmp.path() / "config.json");
    REQUIRE(r.ok);
    CHECK(r.config.store.empty());
    CHECK(r.config.entries.empty());
}

TEST_CASE("save_config_file then load_config_file round-trips") {
    TempDir tmp;
    fs::path path = tmp.path() / "sub" / "config.json";
    Config cfg;
    cfg.store = "/home/user/dotfiles";
    cfg.entries.push_back({".bashrc", "bash/.bashrc", ManageMode::Copy, {}, {}});

    auto save_err = save_config_file(path, cfg);
    REQUIRE_FALSE(save_err.has_value());

    auto r = load_config_file(path);
    REQUIRE(r.ok);
    CHECK(r.config.store == cfg.store);
    REQUIRE(r.config.entries.size() == 1);
    CHECK(r.config.entries[0].home_path == ".bashrc");
}

TEST_CASE("load_config_file never overwrites a malformed file and reports the error") {
    TempDir tmp;
    fs::path path = tmp.path() / "config.json";
    write_file(path, "{not valid json");

    auto r = load_config_file(path);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    // The file on disk must be completely untouched.
    CHECK(read_file(path) == "{not valid json");
}

TEST_CASE("save_config_file creates missing parent directories atomically") {
    TempDir tmp;
    fs::path path = tmp.path() / "a" / "b" / "config.json";
    Config cfg;
    cfg.store = "/home/user/dotfiles";
    auto err = save_config_file(path, cfg);
    CHECK_FALSE(err.has_value());
    CHECK(fs::exists(path));
}

// ---------------------------------------------------------------------------
// default_* path helpers
// ---------------------------------------------------------------------------

TEST_CASE("default_config_path is default_config_dir()/config.json") {
    CHECK(default_config_path() == default_config_dir() / "config.json");
}

TEST_CASE("default_config_dir ends with dotfile-manager") {
    CHECK(default_config_dir().filename() == "dotfile-manager");
}

TEST_CASE("default_state_dir ends with dotfile-manager") {
    CHECK(default_state_dir().filename() == "dotfile-manager");
}
