#include "dfm/hashing.hpp"

#include "dfm/file_types.hpp"
#include "dfm/sha256.hpp"

#include <fstream>
#include <vector>

namespace dfm {

namespace fs = std::filesystem;

std::optional<std::string> hash_file(const fs::path& path, std::string* error) {
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

    Sha256 hasher;
    constexpr std::size_t kChunk = 1 << 16;
    std::vector<char> buf(kChunk);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize got = in.gcount();
        if (got > 0) hasher.update(buf.data(), static_cast<std::size_t>(got));
    }
    if (in.bad()) {
        if (error) *error = "I/O error reading file: " + path.string();
        return std::nullopt;
    }
    return hasher.finalize_hex();
}

std::string hash_bytes(const std::string& data) { return Sha256::hex_of(data); }

}  // namespace dfm
