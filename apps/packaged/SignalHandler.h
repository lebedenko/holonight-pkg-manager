#pragma once

#include <QObject>

// Turns SIGTERM and SIGINT into a Qt signal so the application can leave its event loop normally.
class SignalHandler : public QObject {
  Q_OBJECT

 public:
  explicit SignalHandler(QObject* parent = nullptr);
  ~SignalHandler() override;

  SignalHandler(const SignalHandler&) = delete;
  SignalHandler& operator=(const SignalHandler&) = delete;
  SignalHandler(SignalHandler&&) = delete;
  SignalHandler& operator=(SignalHandler&&) = delete;

 signals:
  void terminationRequested();
};
