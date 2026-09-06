#include "dfm/version.hpp"

namespace dfm {

std::string version_string() { return std::string(kProgramName) + " " + kVersion; }

std::string version_line_full() {
    return version_string() +
           "\n"
           "Safe CLI utility for discovering, comparing, backing up, and restoring dotfiles.\n"
           "License: MIT\n";
}

}  // namespace dfm
