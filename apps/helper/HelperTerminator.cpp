#include "HelperTerminator.h"

#include <QTimer>

namespace holonight_helper {

HelperTerminator::HelperTerminator(FinalizeFunction finalize, std::chrono::milliseconds grace)
    : finalize_(std::move(finalize)), grace_(grace) {}

void HelperTerminator::terminateAfterReply() {
  if (scheduled_) {
    return;
  }
  scheduled_ = true;
  QTimer::singleShot(grace_, Qt::PreciseTimer, [finalize = finalize_] { finalize(); });
}

}  // namespace holonight_helper
