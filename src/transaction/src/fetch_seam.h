#pragma once

#include "holonight_packages_transaction/transaction_seams.h"

#include <alpm.h>

namespace holonight_packages_transaction {

// The one place that installs a libalpm fetch callback (H-004). It only does so for a test seam; the helper never sets
// one, so libalpm's own downloader and the pacman.conf XferCommand stay out of play (REQ-C-003).
inline int fetchSeamTrampoline(void* context, const char* url, const char* destination, int force) {
  const auto* seams = static_cast<const TransactionSeams*>(context);
  return seams->fetch(url != nullptr ? url : "", destination != nullptr ? destination : "", force != 0);
}

inline void installFetchSeam(alpm_handle_t* handle, const TransactionSeams& seams) {
  if (seams.fetch) {
    // libalpm keeps the context as void*; the trampoline only reads it.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    alpm_option_set_fetchcb(handle, &fetchSeamTrampoline, const_cast<TransactionSeams*>(&seams));
  }
}

}  // namespace holonight_packages_transaction
