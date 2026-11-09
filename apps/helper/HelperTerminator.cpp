#include "HelperTerminator.h"

#include <QTimer>

#include <cstdlib>
#include <fstream>
#include <system_error>
#include <unistd.h>

namespace holonight_helper {

HelperTerminator::HelperTerminator(std::filesystem::path lockFile, ExitFunction exit, std::chrono::milliseconds grace)
    : lockFile_(std::move(lockFile)), exit_(exit ? std::move(exit) : [] { std::_Exit(0); }), grace_(grace) {}

void HelperTerminator::terminateAfterReply() {
  std::error_code ignored;
  // libalpm records the owning process ID in db.lck. A cancellation may arrive
  // before our transaction starts, while another package manager owns the lock.
  std::ifstream lock(lockFile_);
  long owner = 0;
  if (lock >> owner && owner == static_cast<long>(::getpid())) {
    std::filesystem::remove(lockFile_, ignored);
  }
  QTimer::singleShot(grace_, [exit = exit_] { exit(); });
}

}  // namespace holonight_helper
