// atomic_io.hpp - atomic file write / copy primitives.
//
// All mutating operations that produce a single regular file destination go
// through atomic_write_file(): write to a temp file in the same directory,
// fsync, then rename() over the destination. rename(2) on the same
// filesystem is atomic, so a concurrent reader (or a crash) never observes a
// partially-written file.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace dfm {

// Writes `content` to `dest` atomically. Creates `dest`'s parent directory
// if missing (only that directory, never recursively creates unrelated
// paths beyond the immediate parent chain that ensure_directory_canonical
// would create). Preserves `mode` permission bits on the created file when
// provided. Returns an error message on failure, std::nullopt on success.
std::optional<std::string> atomic_write_file(const std::filesystem::path& dest, const std::string& content,
                                              std::optional<std::filesystem::perms> mode = std::nullopt);

// Copies a regular file from `src` to `dest` atomically (reads `src` fully,
// writes via atomic_write_file, preserving src's permission bits). Refuses
// non-regular sources. Returns an error message on failure.
std::optional<std::string> atomic_copy_file(const std::filesystem::path& src, const std::filesystem::path& dest);

// Reads an entire regular file into a string. Returns std::nullopt and sets
// *error on failure (missing file, not regular, I/O error).
std::optional<std::string> read_file_to_string(const std::filesystem::path& path, std::string* error = nullptr);

}  // namespace dfm
