#include "dfm/atomic_io.hpp"

#include "dfm/file_types.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

namespace dfm {

namespace fs = std::filesystem;

namespace {

std::string random_suffix() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(12);
    for (int i = 0; i < 12; ++i) s.push_back(hex[dist(rng)]);
    return s;
}

}  // namespace

std::optional<std::string> atomic_write_file(const fs::path& dest, const std::string& content,
                                              std::optional<fs::perms> mode) {
    fs::path parent = dest.parent_path();
    if (parent.empty()) parent = ".";

    std::error_code ec;
    if (!fs::exists(parent, ec)) {
        fs::create_directories(parent, ec);
        if (ec) return std::string("failed to create parent directory '") + parent.string() + "': " + ec.message();
    }

    fs::path tmp = parent / (std::string(".") + dest.filename().string() + ".tmp-" + random_suffix());

    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return std::string("failed to create temporary file '") + tmp.string() + "': " + std::strerror(errno);
    }

    {
        std::size_t written = 0;
        const char* data = content.data();
        std::size_t remaining = content.size();
        while (remaining > 0) {
            ssize_t n = ::write(fd, data + written, remaining);
            if (n < 0) {
                if (errno == EINTR) continue;
                std::string err = std::strerror(errno);
                ::close(fd);
                fs::remove(tmp, ec);
                return "write failed for '" + tmp.string() + "': " + err;
            }
            written += static_cast<std::size_t>(n);
            remaining -= static_cast<std::size_t>(n);
        }
    }

    if (mode) {
        ::fchmod(fd, static_cast<mode_t>(*mode) & 0777);
    }

    if (::fsync(fd) != 0) {
        std::string err = std::strerror(errno);
        ::close(fd);
        fs::remove(tmp, ec);
        return "fsync failed for '" + tmp.string() + "': " + err;
    }
    ::close(fd);

    fs::rename(tmp, dest, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return "atomic rename failed ('" + tmp.string() + "' -> '" + dest.string() + "'): " + ec.message();
    }

    // Best-effort: fsync the containing directory so the rename is durable.
    int dfd = ::open(parent.c_str(), O_RDONLY);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }

    return std::nullopt;
}

std::optional<std::string> read_file_to_string(const fs::path& path, std::string* error) {
    auto info = inspect(path);
    if (info.kind == EntryKind::NotFound) {
        if (error) *error = "file not found: " + path.string();
        return std::nullopt;
    }
    if (info.kind != EntryKind::Regular) {
        if (error) *error = "not a regular file: " + path.string();
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "failed to open file: " + path.string();
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) {
        if (error) *error = "I/O error reading file: " + path.string();
        return std::nullopt;
    }
    return ss.str();
}

std::optional<std::string> atomic_copy_file(const fs::path& src, const fs::path& dest) {
    auto info = inspect(src);
    if (info.kind == EntryKind::NotFound) return "source not found: " + src.string();
    if (info.kind != EntryKind::Regular) return "refusing to copy non-regular file: " + src.string();

    std::string err;
    auto content = read_file_to_string(src, &err);
    if (!content) return err;

    fs::perms mode = info.permissions;
    return atomic_write_file(dest, *content, mode);
}

}  // namespace dfm
