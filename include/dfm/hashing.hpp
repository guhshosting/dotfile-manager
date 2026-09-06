// hashing.hpp - file content hashing built on the internal SHA-256 primitive.
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace dfm {

// Computes the lowercase hex SHA-256 digest of a regular file's contents,
// streaming it in fixed-size chunks so large files do not need to be loaded
// into memory at once. Returns std::nullopt (with *error set) if the path is
// not a regular file or cannot be read.
std::optional<std::string> hash_file(const std::filesystem::path& path, std::string* error = nullptr);

// Computes the lowercase hex SHA-256 digest of an in-memory buffer.
std::string hash_bytes(const std::string& data);

}  // namespace dfm
