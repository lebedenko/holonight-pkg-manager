#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class UpdateStatusPresentation : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Update status is provided by the application")
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(int count READ count NOTIFY changed)
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(QString badgeText READ badgeText NOTIFY changed)

 public:
  using QObject::QObject;

  [[nodiscard]] bool available() const { return available_; }
  [[nodiscard]] int count() const { return count_; }
  [[nodiscard]] const QString& state() const { return state_; }
  [[nodiscard]] QString badgeText() const {
    return available_ && state_ == QStringLiteral("ready") && count_ > 0 ? QString::number(count_) : QString();
  }

 signals:
  void changed();

 protected:
  void setValues(bool available, int count, const QString& state) {
    if (available_ == available && count_ == count && state_ == state) {
      return;
    }
    available_ = available;
    count_ = count;
    state_ = state;
    emit changed();
  }

 private:
  bool available_ = false;
  int count_ = 0;
  QString state_;
};
