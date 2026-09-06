// test_helpers.hpp - shared fixtures for the unit test suite.
//
// IMPORTANT: every test operates entirely inside a freshly created temporary
// directory. Nothing here ever touches the developer's real HOME or any
// system path. TempDir uses std::filesystem::remove_all only on a path it
// itself created under the system temp directory - this is test-harness
// cleanup code, not part of the dotfile-manager production code path (which
// never calls remove_all on a user-derived path).
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>

namespace dfm_test {

namespace fs = std::filesystem;

inline fs::path make_unique_temp_dir(const std::string& prefix) {
    static std::mt19937_64 rng{std::random_device{}()};
    for (;;) {
        std::ostringstream name;
        name << prefix << "-" << rng();
        fs::path candidate = fs::temp_directory_path() / name.str();
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) {
            return fs::canonical(candidate);
        }
    }
}

// RAII temporary directory, recursively removed on destruction. Only ever
// operates on a path it created itself under the system temp directory.
class TempDir {
public:
    explicit TempDir(const std::string& prefix = "dfm-test") : path_(make_unique_temp_dir(prefix)) {}
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

inline void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
}

inline std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline void make_symlink(const fs::path& target, const fs::path& link) {
    fs::create_directories(link.parent_path());
    std::error_code ec;
    fs::remove(link, ec);
    fs::create_symlink(target, link);
}

inline void make_dir_symlink(const fs::path& target, const fs::path& link) {
    fs::create_directories(link.parent_path());
    std::error_code ec;
    fs::remove(link, ec);
    fs::create_directory_symlink(target, link);
}

// RAII helper that changes the process current working directory for its
// lifetime, restoring the original directory on destruction. Needed because
// resolve_user_path_to_home_relative() (like most real CLI tools) resolves
// bare relative paths against the process CWD, not against $HOME - matching
// normal shell usage where a user runs `dotfile-manager add .bashrc` from
// inside their home directory.
class ScopedCwd {
public:
    explicit ScopedCwd(const fs::path& new_cwd) : original_(fs::current_path()) { fs::current_path(new_cwd); }
    ~ScopedCwd() {
        std::error_code ec;
        fs::current_path(original_, ec);
    }
    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;

private:
    fs::path original_;
};

// A fixture that sets up an isolated fake HOME with an isolated fake store
// directory, both under the same TempDir. Never touches real HOME/XDG state.
// For the duration of the fixture's lifetime, the process CWD is also
// switched to the fake home directory (and restored on destruction) so that
// relative-path CLI arguments resolve the same way they would for a real
// user working from their home directory.
struct FakeHome {
    TempDir root{"dfm-home"};
    fs::path home = root.path() / "home";
    fs::path store = root.path() / "store";

    FakeHome() {
        fs::create_directories(home);
        // store is intentionally NOT created here in most tests, mirroring
        // the real "store does not exist yet" first-run scenario.
        cwd_.emplace(home);
    }

    fs::path h(const std::string& rel) const { return home / rel; }
    fs::path s(const std::string& rel) const { return store / rel; }

private:
    // Declared last so it is destroyed FIRST (reverse declaration order),
    // restoring the original CWD before `root`'s destructor removes the
    // temporary directory tree the process might otherwise still be inside.
    std::optional<ScopedCwd> cwd_;
};

}  // namespace dfm_test
