#pragma once

#include "holonight_packages_transaction/log_sink.h"

namespace holonight_helper {

// Writes `<N>text` lines to standard error, which systemd's journal reads as priorities (OI-12). Every line has its URL
// credentials redacted before it is written (REQ-NF-004).
[[nodiscard]] holonight_packages_transaction::LogSink journalSink();

// The same sink writing into `output` (a file descriptor), for tests.
[[nodiscard]] holonight_packages_transaction::LogSink journalSinkTo(int descriptor);

}  // namespace holonight_helper
