#include "dfm/env.hpp"

#include <cstdlib>
#include <unistd.h>

namespace dfm {

namespace fs = std::filesystem;

std::optional<std::string> get_env(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') return std::nullopt;
    return std::string(v);
}

std::optional<fs::path> home_dir() {
    auto h = get_env("HOME");
    if (!h) return std::nullopt;
    fs::path p(*h);
    if (!p.is_absolute()) return std::nullopt;
    return p;
}

fs::path xdg_config_home() {
    if (auto v = get_env("XDG_CONFIG_HOME")) {
        fs::path p(*v);
        if (p.is_absolute()) return p;
    }
    if (auto h = home_dir()) return *h / ".config";
    return fs::path(".config");
}

fs::path xdg_state_home() {
    if (auto v = get_env("XDG_STATE_HOME")) {
        fs::path p(*v);
        if (p.is_absolute()) return p;
    }
    if (auto h = home_dir()) return *h / ".local" / "state";
    return fs::path(".local/state");
}

bool running_as_root() { return geteuid() == 0; }

}  // namespace dfm
