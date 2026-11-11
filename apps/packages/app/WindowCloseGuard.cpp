#include "WindowCloseGuard.h"

#include "TransactionController.h"

#include <QCloseEvent>
#include <QWindow>

WindowCloseGuard::WindowCloseGuard(TransactionController& controller, QWindow& window)
    : QObject(&window), controller_(&controller), window_(&window) {
  window_->installEventFilter(this);
  connect(controller_, &TransactionController::closeReady, this, &WindowCloseGuard::onCloseReady);
}

bool WindowCloseGuard::eventFilter(QObject* watched, QEvent* event) {
  if (watched != window_ || event->type() != QEvent::Close) {
    return QObject::eventFilter(watched, event);
  }
  if (allow_next_close_) {
    allow_next_close_ = false;
    return false;  // let the close through
  }
  using holonight_packages_application::CloseDecision;
  switch (controller_->handleCloseRequest()) {
    case CloseDecision::AcceptNow:
      return false;
    case CloseDecision::DeferUntilCancelReply:
    case CloseDecision::Ignore:
      event->ignore();
      return true;
  }
  return false;
}

void WindowCloseGuard::onCloseReady() {
  allow_next_close_ = true;
  if (!window_->close()) {
    allow_next_close_ = false;  // not visible: nothing to close
  }
}
