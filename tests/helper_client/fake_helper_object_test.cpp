#include "helper_bus_fixture.h"

namespace {

using holonight_packages_testing::FakeHelperObject;
using holonight_packages_testing::HelperBusFixture;

class HelperClientBus : public HelperBusFixture {};

TEST_F(HelperClientBus, TheFakeAnswersAScriptedExecuteReply) {
  FakeHelperObject& fake = startFakeHelper();
  helper_wire::ResultWire result;
  result.outcome = QStringLiteral("Cancelled");
  fake.script(QStringLiteral("Execute"),
              {.mode = FakeHelperObject::Mode::Immediate, .delay_ms = 0, .reply = {QVariant::fromValue(result)}});
  QDBusConnection client = connect(QStringLiteral("client"));
  const QDBusMessage reply = call(client, QStringLiteral("Execute"), {QStringLiteral("plan-1")});
  EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage);
  EXPECT_EQ(fake.calls(QStringLiteral("Execute")), 1);
}

TEST_F(HelperClientBus, TheFakeRejectsAPreparePlanCallWithAnExtraArgumentUsingInvalidArgs) {
  FakeHelperObject& fake = startFakeHelper();
  QDBusConnection client = connect(QStringLiteral("client"));
  const QVariantMap options;
  const QDBusMessage reply =
      call(client, QStringLiteral("PreparePlan"),
           {QStringLiteral("install"), QStringList{QStringLiteral("app")}, options, QStringLiteral("extra")});
  ASSERT_EQ(reply.type(), QDBusMessage::ErrorMessage);
  EXPECT_EQ(reply.errorName(), QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));
  EXPECT_EQ(fake.calls(QStringLiteral("PreparePlan")), 0);
  EXPECT_EQ(fake.rejected(), QStringList{QStringLiteral("PreparePlan")});
}

TEST_F(HelperClientBus, TheFakeAcceptsTheDeclaredPreparePlanSignature) {
  FakeHelperObject& fake = startFakeHelper();
  helper_wire::StatusWire status;
  helper_wire::PreviewWire preview;
  preview.action = QStringLiteral("install");
  fake.script(QStringLiteral("PreparePlan"),
              {
                  .mode = FakeHelperObject::Mode::Immediate,
                  .delay_ms = 0,
                  .reply = {QVariant::fromValue(status), QStringLiteral("plan-1"), QVariant::fromValue(preview)},
              });
  QDBusConnection client = connect(QStringLiteral("client"));
  const QVariantMap options;
  const QDBusMessage reply = call(client, QStringLiteral("PreparePlan"),
                                  {QStringLiteral("install"), QStringList{QStringLiteral("app")}, options});
  EXPECT_EQ(reply.type(), QDBusMessage::ReplyMessage);
  EXPECT_EQ(fake.calls(QStringLiteral("PreparePlan")), 1);
}

}  // namespace
