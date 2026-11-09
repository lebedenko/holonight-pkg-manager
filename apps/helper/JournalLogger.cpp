#include "JournalLogger.h"

#include <string>
#include <unistd.h>

namespace holonight_helper {
namespace {

using holonight_packages_transaction::LogLevel;

const char* priority(LogLevel level) {
  switch (level) {
    case LogLevel::Error:
      return "<3>";
    case LogLevel::Warning:
      return "<4>";
    case LogLevel::Info:
      return "<6>";
  }
  return "<6>";
}

}  // namespace

holonight_packages_transaction::LogSink journalSinkTo(int descriptor) {
  return [descriptor](LogLevel level, const std::string& text) {
    std::string line = priority(level) + holonight_packages_transaction::redactUrlCredentials(text);
    // One line per message: a newline inside a message would otherwise start a second journal entry.
    for (char& character : line) {
      if (character == '\n' || character == '\r') {
        character = ' ';
      }
    }
    line.push_back('\n');
    [[maybe_unused]] const ssize_t written = ::write(descriptor, line.data(), line.size());
  };
}

holonight_packages_transaction::LogSink journalSink() { return journalSinkTo(STDERR_FILENO); }

}  // namespace holonight_helper
