#pragma once

#include <atomic>
#include <gtest/gtest.h>

namespace holonight_transaction_testing {

// libalpm initialises gpgme once per process, with the keyring directory of the first handle that verifies a signature.
// A later handle with another GPGDir silently keeps using the first one. ctest starts every test in its own process, so
// each signature-dependent test gets a fresh gpgme; when several run in one process, only the first can be trusted.
inline bool claimGpgmeForThisProcess() {
  static std::atomic<bool> claimed{false};
  return !claimed.exchange(true);
}

}  // namespace holonight_transaction_testing

// Put this first in any test that makes libalpm verify a package or database signature.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): GTEST_SKIP must return from the test body itself.
#define SKIP_UNLESS_FRESH_GPGME()                                                                         \
  if (!::holonight_transaction_testing::claimGpgmeForThisProcess()) {                                     \
    GTEST_SKIP() << "libalpm initialises gpgme once per process; run this test on its own (ctest does)."; \
  }
