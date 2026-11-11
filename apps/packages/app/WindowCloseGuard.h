#pragma once

#include <QObject>

class QEvent;
class QWindow;
class TransactionController;

// Holds the window open while a transaction needs it (REQ-F-033..035). The page tree's root is an Item inside a
// QQuickView, so there is no QML Window to hang an onClosing handler on; this is a small event filter on the window.
//
//  - AcceptNow:             the close event passes through.
//  - DeferUntilCancelReply: the event is ignored; when the controller signals closeReady the guard closes the window.
//  - Ignore:                the event is ignored (the controller has set the close-unavailable notice).
class WindowCloseGuard : public QObject {
  Q_OBJECT

 public:
  WindowCloseGuard(TransactionController& controller, QWindow& window);
  ~WindowCloseGuard() override = default;

  WindowCloseGuard(const WindowCloseGuard&) = delete;
  WindowCloseGuard& operator=(const WindowCloseGuard&) = delete;
  WindowCloseGuard(WindowCloseGuard&&) = delete;
  WindowCloseGuard& operator=(WindowCloseGuard&&) = delete;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void onCloseReady();

  TransactionController* controller_;
  QWindow* window_;
  bool allow_next_close_ = false;
};
