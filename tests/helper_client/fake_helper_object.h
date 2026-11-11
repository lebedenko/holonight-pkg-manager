#pragma once

#include "HelperWire.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QFile>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QXmlStreamReader>

namespace holonight_packages_testing {

// A scripted stand-in for the package helper on a private bus. It reads the interface XML, learns the input
// signature of each method, and answers InvalidArgs to any call whose signature differs (REQ-C-004: the adapter
// sends only the declared arguments). It counts calls per method and replies as scripted.
class FakeHelperObject final : public QDBusVirtualObject {
 public:
  enum class Mode : quint8 { Immediate, Delayed, Never };

  struct Script {
    Mode mode = Mode::Immediate;
    int delay_ms = 0;
    QVariantList reply;
  };

  explicit FakeHelperObject(QDBusConnection connection, QObject* parent = nullptr)
      : QDBusVirtualObject(parent), connection_(std::move(connection)) {
    loadSignatures();
  }

  [[nodiscard]] static QString servicePath() { return QStringLiteral("/org/holonight/PackageHelper1"); }
  [[nodiscard]] static QString interfaceName() { return QStringLiteral("org.holonight.PackageHelper1.Transactions"); }
  [[nodiscard]] static QString serviceName() { return QStringLiteral("org.holonight.PackageHelper1"); }

  // Owns the well-known name and registers the object. Returns false on failure.
  [[nodiscard]] bool start() {
    return connection_.registerVirtualObject(servicePath(), this, QDBusConnection::SingleNode) &&
           connection_.registerService(serviceName());
  }

  void script(const QString& method, Script script) { scripts_.insert(method, std::move(script)); }
  [[nodiscard]] int calls(const QString& method) const { return calls_.value(method, 0); }
  [[nodiscard]] QStringList rejected() const { return rejected_; }
  [[nodiscard]] QList<QVariantList> argumentsOf(const QString& method) const { return arguments_.value(method); }

  // Emits the Progress signal. With a destination (a unique bus name) it is unicast, as the real helper does.
  void emitProgress(const QString& plan_id, const QString& phase, uint current, uint total, const QString& item,
                    const QString& destination = {}) {
    QDBusMessage signal = destination.isEmpty()
                              ? QDBusMessage::createSignal(servicePath(), interfaceName(), QStringLiteral("Progress"))
                              : QDBusMessage::createTargetedSignal(destination, servicePath(), interfaceName(),
                                                                   QStringLiteral("Progress"));
    signal << plan_id << phase << current << total << item;
    connection_.send(signal);
  }

  [[nodiscard]] QString introspect(const QString& /*path*/) const override { return {}; }

  bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override {
    if (message.type() != QDBusMessage::MethodCallMessage || message.interface() != interfaceName()) {
      return false;
    }
    const QString method = message.member();
    const auto expected = inputs_.constFind(method);
    if (expected == inputs_.cend()) {
      connection.send(message.createErrorReply(QDBusError::UnknownMethod, method));
      return true;
    }
    if (message.signature() != expected.value()) {
      rejected_.push_back(method);
      connection.send(message.createErrorReply(
          QDBusError::InvalidArgs,
          QStringLiteral("signature %1, expected %2").arg(message.signature(), expected.value())));
      return true;
    }
    ++calls_[method];
    arguments_[method].push_back(message.arguments());
    const Script scripted = scripts_.value(method);
    if (scripted.mode == Mode::Never) {
      return true;
    }
    const QDBusMessage reply = message.createReply(scripted.reply);
    if (scripted.mode == Mode::Immediate) {
      connection.send(reply);
      return true;
    }
    const QDBusConnection& target = connection;
    QTimer::singleShot(scripted.delay_ms, this, [target, reply] { target.send(reply); });
    return true;
  }

 private:
  void loadSignatures() {
    QFile file(QString::fromLatin1(HOLONIGHT_HELPER_DBUS_XML));
    if (!file.open(QIODevice::ReadOnly)) {
      return;
    }
    QXmlStreamReader xml(&file);
    QString method;
    while (!xml.atEnd()) {
      xml.readNext();
      if (xml.isStartElement() && xml.name() == QLatin1String("method")) {
        method = xml.attributes().value(QLatin1String("name")).toString();
        inputs_.insert(method, QString());
      } else if (xml.isEndElement() && xml.name() == QLatin1String("method")) {
        method.clear();
      } else if (!method.isEmpty() && xml.isStartElement() && xml.name() == QLatin1String("arg") &&
                 xml.attributes().value(QLatin1String("direction")) == QLatin1String("in")) {
        inputs_[method] += xml.attributes().value(QLatin1String("type")).toString();
      }
    }
  }

  QDBusConnection connection_;
  QHash<QString, QString> inputs_;
  QHash<QString, Script> scripts_;
  QHash<QString, int> calls_;
  QHash<QString, QList<QVariantList>> arguments_;
  QStringList rejected_;
};

}  // namespace holonight_packages_testing
