// version.hpp - build/version identification.
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace dfm {

constexpr const char* kVersion = "1.0.0";
constexpr const char* kProgramName = "dotfile-manager";

std::string version_string();     // "dotfile-manager 1.0.0"
std::string version_line_full();  // multi-line version banner incl. license

}  // namespace dfm
