// exit_codes.hpp - deterministic process exit codes. See README.md.
// SPDX-License-Identifier: MIT
#pragma once

namespace dfm {

enum ExitCode {
    kExitSuccess = 0,             // operation successful / everything in sync
    kExitDifferences = 1,         // differences or nonfatal warnings
    kExitUsageError = 2,          // CLI usage error
    kExitConfigError = 3,         // configuration error
    kExitPathSafetyError = 4,     // filesystem/path safety error
    kExitUnresolvedConflict = 5,  // unresolved conflict
    kExitIoFailure = 6,           // I/O failure
    kExitInternalError = 7,       // internal error
};

}  // namespace dfm
