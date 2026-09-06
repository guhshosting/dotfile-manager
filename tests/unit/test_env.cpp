// test_env.cpp - unit tests for dfm/env.hpp.
//
// These tests manipulate environment variables via setenv/unsetenv scoped to
// the test process. They never read or rely on the developer's real HOME.
//
// SPDX-License-Identifier: MIT
#include "dfm/env.hpp"

#include "doctest.h"
#include "test_helpers.hpp"

#include <cstdlib>

using namespace dfm;
using namespace dfm_test;

namespace {

struct EnvVarGuard {
    std::string name;
    bool had_value;
    std::string old_value;

    explicit EnvVarGuard(std::string n) : name(std::move(n)) {
        const char* v = std::getenv(name.c_str());
        had_value = v != nullptr;
        if (had_value) old_value = v;
    }
    ~EnvVarGuard() {
        if (had_value)
            setenv(name.c_str(), old_value.c_str(), 1);
        else
            unsetenv(name.c_str());
    }
    void set(const std::string& v) { setenv(name.c_str(), v.c_str(), 1); }
    void unset() { unsetenv(name.c_str()); }
};

}  // namespace

TEST_CASE("home_dir returns HOME when it is a valid absolute path") {
    EnvVarGuard guard("HOME");
    TempDir tmp;
    guard.set(tmp.path().string());
    auto h = home_dir();
    REQUIRE(h.has_value());
    CHECK(fs::equivalent(*h, tmp.path()));
}

TEST_CASE("home_dir returns nullopt when HOME is unset") {
    EnvVarGuard guard("HOME");
    guard.unset();
    auto h = home_dir();
    CHECK_FALSE(h.has_value());
}

TEST_CASE("home_dir returns nullopt when HOME is empty") {
    EnvVarGuard guard("HOME");
    guard.set("");
    auto h = home_dir();
    CHECK_FALSE(h.has_value());
}

TEST_CASE("home_dir returns nullopt when HOME is relative") {
    EnvVarGuard guard("HOME");
    guard.set("relative/path");
    auto h = home_dir();
    CHECK_FALSE(h.has_value());
}

TEST_CASE("xdg_config_home honors XDG_CONFIG_HOME when set") {
    EnvVarGuard guard("XDG_CONFIG_HOME");
    guard.set("/tmp/custom-config");
    CHECK(xdg_config_home() == fs::path("/tmp/custom-config"));
}

TEST_CASE("xdg_config_home falls back to HOME/.config when unset") {
    EnvVarGuard xdgGuard("XDG_CONFIG_HOME");
    EnvVarGuard homeGuard("HOME");
    xdgGuard.unset();
    homeGuard.set("/tmp/fakehome");
    CHECK(xdg_config_home() == fs::path("/tmp/fakehome/.config"));
}

TEST_CASE("xdg_state_home honors XDG_STATE_HOME when set") {
    EnvVarGuard guard("XDG_STATE_HOME");
    guard.set("/tmp/custom-state");
    CHECK(xdg_state_home() == fs::path("/tmp/custom-state"));
}

TEST_CASE("xdg_state_home falls back to HOME/.local/state when unset") {
    EnvVarGuard xdgGuard("XDG_STATE_HOME");
    EnvVarGuard homeGuard("HOME");
    xdgGuard.unset();
    homeGuard.set("/tmp/fakehome");
    CHECK(xdg_state_home() == fs::path("/tmp/fakehome/.local/state"));
}

TEST_CASE("get_env returns nullopt for an unset variable") {
    EnvVarGuard guard("DFM_TEST_UNSET_VAR_XYZ");
    guard.unset();
    CHECK_FALSE(get_env("DFM_TEST_UNSET_VAR_XYZ").has_value());
}

TEST_CASE("get_env returns the value for a set variable") {
    EnvVarGuard guard("DFM_TEST_SET_VAR_XYZ");
    guard.set("hello");
    auto v = get_env("DFM_TEST_SET_VAR_XYZ");
    REQUIRE(v.has_value());
    CHECK(*v == "hello");
}

TEST_CASE("get_env returns nullopt for an empty variable") {
    EnvVarGuard guard("DFM_TEST_EMPTY_VAR_XYZ");
    guard.set("");
    CHECK_FALSE(get_env("DFM_TEST_EMPTY_VAR_XYZ").has_value());
}

TEST_CASE("running_as_root returns a stable boolean without throwing") {
    // We cannot force root/non-root in the test sandbox, but the call must
    // not throw and must be consistent across repeated calls in-process.
    bool first = running_as_root();
    bool second = running_as_root();
    CHECK(first == second);
}
