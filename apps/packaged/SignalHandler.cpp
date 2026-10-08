#include "SignalHandler.h"

#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int signal_pipe[2] = {-1, -1};  // NOLINT(cppcoreguidelines-avoid-c-arrays,readability-identifier-naming)

void onSignal(int /*signal*/) {
  const char byte = 1;
  [[maybe_unused]] const ssize_t written = ::write(signal_pipe[0], &byte, 1);
}

}  // namespace

SignalHandler::SignalHandler(QObject* parent) : QObject(parent) {
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, signal_pipe) != 0) {
    return;
  }
  auto* notifier = new QSocketNotifier(signal_pipe[1], QSocketNotifier::Read, this);
  connect(notifier, &QSocketNotifier::activated, this, [this] {
    char byte = 0;
    [[maybe_unused]] const ssize_t received = ::read(signal_pipe[1], &byte, 1);
    emit terminationRequested();
  });
  struct sigaction action{};
  action.sa_handler = onSignal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);
}

SignalHandler::~SignalHandler() {
  std::signal(SIGTERM, SIG_DFL);
  std::signal(SIGINT, SIG_DFL);
  ::close(signal_pipe[0]);
  ::close(signal_pipe[1]);
  signal_pipe[0] = signal_pipe[1] = -1;
}
