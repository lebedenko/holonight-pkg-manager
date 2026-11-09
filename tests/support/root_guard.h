#pragma once

#include <iostream>
#include <unistd.h>

namespace holonight_test_support {

// REQ-C-010: the helper tests never need root and refuse to run as root, so a mistake cannot touch the real system.
[[nodiscard]] inline bool runningAsRoot() { return ::geteuid() == 0; }

// Returns a non-zero process exit code when the process runs as root, and zero otherwise.
[[nodiscard]] inline int refuseToRunAsRoot(const char* program) {
  if (!runningAsRoot()) {
    return 0;
  }
  std::cerr << program << ": refusing to run as root (REQ-C-010)\n";
  return 1;
}

}  // namespace holonight_test_support
