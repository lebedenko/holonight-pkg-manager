#include "PolkitAuthorizer.h"
#include "private_bus.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QEventLoop>
#include <QTimer>

#include <atomic>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>

namespace holonight_helper {
namespace {

using holonight_packages_application::AuthorizationDecision;
using holonight_packages_application::AuthorizationRequest;
using holonight_packages_application::SubjectKind;
using holonight_packages_testing::PrivateBus;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

void pump(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

struct Seen {
  PolkitSubject subject;
  QString action_id;
  QMap<QString, QString> details;
  uint flags = 0;
  QString cancellation_id;
};

// Stands in for polkitd: org.freedesktop.PolicyKit1.Authority on the private bus.
class FakeAuthority : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.PolicyKit1.Authority")

 public:
  enum class Mode : std::uint8_t { Authorized, NotAuthorized, Challenge, Error, Never };
  explicit FakeAuthority(QObject* parent) : QDBusAbstractAdaptor(parent) {}

  Mode mode = Mode::Authorized;
  QDBusConnection connection{QStringLiteral("unused")};  // the connection the replies leave on
  QList<Seen> calls;
  QStringList cancelled;

 public slots:
  // NOLINTNEXTLINE(readability-identifier-naming)
  PolkitResult CheckAuthorization(const PolkitSubject& subject, const QString& action_id,
                                  const QMap<QString, QString>& details, uint flags, const QString& cancellation_id,
                                  const QDBusMessage& message) {
    calls.push_back(Seen{
        .subject = subject,
        .action_id = action_id,
        .details = details,
        .flags = flags,
        .cancellation_id = cancellation_id,
    });
    PolkitResult result;
    switch (mode) {
      case Mode::Authorized:
        result.is_authorized = true;
        break;
      case Mode::Challenge:
        result.is_challenge = true;
        break;
      case Mode::NotAuthorized:
        break;
      case Mode::Error:
        message.setDelayedReply(true);
        connection.send(message.createErrorReply(QStringLiteral("org.freedesktop.PolicyKit1.Error.Failed"),
                                                 QStringLiteral("boom")));
        break;
      case Mode::Never:
        message.setDelayedReply(true);  // the prompt stays open
        break;
    }
    return result;
  }
  // NOLINTNEXTLINE(readability-identifier-naming)
  void CancelCheckAuthorization(const QString& cancellation_id) { cancelled.push_back(cancellation_id); }
};

class PolkitTest : public ::testing::Test {
 public:
  void SetUp() override {
    registerPolkitTypes();
    ASSERT_TRUE(bus_.start()) << bus_.error().toStdString();
    static std::atomic<int> counter{0};
    const QString suffix = QString::number(counter++);
    daemonSide_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("fake-polkitd-") + suffix);
    helperSide_ = QDBusConnection::connectToBus(bus_.address(), QStringLiteral("helper-side-") + suffix);
    authority_ = new QObject;
    fake_ = new FakeAuthority(authority_);
    fake_->connection = daemonSide_;
    ASSERT_TRUE(daemonSide_.registerObject(QStringLiteral("/org/freedesktop/PolicyKit1/Authority"), authority_,
                                           QDBusConnection::ExportAdaptors));
    ASSERT_TRUE(daemonSide_.registerService(QStringLiteral("org.freedesktop.PolicyKit1")));
    authorizer_ = std::make_unique<PolkitAuthorizer>(helperSide_, 600);
  }
  void TearDown() override {
    authorizer_.reset();
    delete authority_;
    const QString daemon = daemonSide_.name();
    const QString helper = helperSide_.name();
    daemonSide_ = QDBusConnection(QStringLiteral("unused"));
    helperSide_ = QDBusConnection(QStringLiteral("unused"));
    QDBusConnection::disconnectFromBus(daemon);
    QDBusConnection::disconnectFromBus(helper);
  }

  [[nodiscard]] static AuthorizationRequest request() {
    AuthorizationRequest value;
    value.bus_name = ":1.77";
    value.action_id = "org.holonight.packages.install";
    value.allow_user_interaction = true;
    value.cancellation_id = "0123456789abcdef0123456789abcdef";
    value.details = {{"packages", "app lib"}};
    return value;
  }

  [[nodiscard]] std::optional<AuthorizationDecision> ask(const AuthorizationRequest& value, int waitMs = 3000) const {
    std::optional<AuthorizationDecision> decision;
    authorizer_->check(value, [&decision](AuthorizationDecision verdict) { decision = verdict; });
    for (int waited = 0; waited < waitMs && !decision; waited += 10) {
      pump(10);
    }
    return decision;
  }

  PrivateBus bus_;
  QDBusConnection daemonSide_{QStringLiteral("unused")};
  QDBusConnection helperSide_{QStringLiteral("unused")};
  QObject* authority_ = nullptr;
  FakeAuthority* fake_ = nullptr;
  std::unique_ptr<PolkitAuthorizer> authorizer_;
};

TEST_F(PolkitTest, TheSubjectIsTheCallersUniqueBusNameWithUserInteractionAndNoProcessId) {
  fake_->mode = FakeAuthority::Mode::Authorized;
  EXPECT_EQ(ask(request()), AuthorizationDecision::Authorized);
  ASSERT_EQ(fake_->calls.size(), 1);
  const Seen& seen = fake_->calls[0];
  EXPECT_EQ(seen.subject.kind, QStringLiteral("system-bus-name"));
  EXPECT_EQ(seen.subject.details.value(QStringLiteral("name")).toString(), QStringLiteral(":1.77"));
  EXPECT_EQ(seen.subject.details.size(), 1);  // no pid, no start-time
  EXPECT_FALSE(seen.subject.details.contains(QStringLiteral("pid")));
  EXPECT_EQ(seen.action_id, QStringLiteral("org.holonight.packages.install"));
  EXPECT_EQ(seen.flags, 1U);  // AllowUserInteraction
  EXPECT_EQ(seen.cancellation_id, QStringLiteral("0123456789abcdef0123456789abcdef"));
  EXPECT_EQ(seen.details.value(QStringLiteral("packages")), QStringLiteral("app lib"));
}

TEST_F(PolkitTest, OnlyAnExplicitAuthorizationIsAuthorized) {
  fake_->mode = FakeAuthority::Mode::NotAuthorized;
  EXPECT_EQ(ask(request()), AuthorizationDecision::NotAuthorized);
  fake_->mode = FakeAuthority::Mode::Challenge;
  EXPECT_EQ(ask(request()), AuthorizationDecision::Challenge);
}

TEST_F(PolkitTest, ADBusErrorIsAnErrorDecision) {
  fake_->mode = FakeAuthority::Mode::Error;
  EXPECT_EQ(ask(request()), AuthorizationDecision::Error);
}

TEST_F(PolkitTest, ANonAnsweringPolkitTimesOutAsAnError) {
  fake_->mode = FakeAuthority::Mode::Never;
  EXPECT_EQ(ask(request(), 5000), AuthorizationDecision::Error);
}

TEST_F(PolkitTest, WithoutUserInteractionTheFlagIsZero) {
  AuthorizationRequest value = request();
  value.allow_user_interaction = false;
  ASSERT_TRUE(ask(value).has_value());
  EXPECT_EQ(fake_->calls.at(0).flags, 0U);
}

TEST_F(PolkitTest, CancellingTellsPolkitAndSuppressesTheLateAnswer) {
  fake_->mode = FakeAuthority::Mode::Never;
  std::optional<AuthorizationDecision> decision;
  authorizer_->check(request(), [&decision](AuthorizationDecision verdict) { decision = verdict; });
  pump(100);
  authorizer_->cancel("0123456789abcdef0123456789abcdef");
  pump(1000);  // longer than the call timeout
  EXPECT_FALSE(decision.has_value());
  EXPECT_THAT(fake_->cancelled, ElementsAre(QStringLiteral("0123456789abcdef0123456789abcdef")));
}

TEST_F(PolkitTest, AMissingPolkitIsAnError) {
  daemonSide_.unregisterService(QStringLiteral("org.freedesktop.PolicyKit1"));
  pump(100);
  EXPECT_EQ(ask(request()), AuthorizationDecision::Error);
}

}  // namespace
}  // namespace holonight_helper

#include "polkit_authorizer_test.moc"
