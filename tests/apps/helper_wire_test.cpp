#include "HelperWire.h"

#include <QDBusMetaType>
#include <QFile>
#include <QVariant>
#include <QXmlStreamReader>

#include <gtest/gtest.h>

namespace {

using namespace helper_wire;
using namespace holonight_packages_domain;

QDBusMessage replyWith(const QVariantList& arguments) {
  QDBusMessage message = QDBusMessage::createMethodCall(
      QStringLiteral("org.holonight.PackageHelper1"), QStringLiteral("/org/holonight/PackageHelper1"),
      QStringLiteral("org.holonight.PackageHelper1.Transactions"), QStringLiteral("PreparePlan"));
  return message.createReply(arguments);
}

PreviewWire installPreview() {
  PreviewWire preview;
  preview.action = QStringLiteral("install");
  preview.targets.push_back({
      .name = QStringLiteral("app"),
      .action = QStringLiteral("install"),
      .old_version = QString(),
      .new_version = QStringLiteral("1-1"),
      .repository = QStringLiteral("extra"),
  });
  preview.dependencies.push_back({
      .name = QStringLiteral("lib"),
      .action = QStringLiteral("install"),
      .old_version = QString(),
      .new_version = QStringLiteral("2-1"),
      .repository = QStringLiteral("core"),
  });
  preview.orphans.push_back({
      .name = QStringLiteral("old"),
      .action = QStringLiteral("remove"),
      .old_version = QStringLiteral("3-1"),
      .new_version = QString(),
      .repository = QString(),
  });
  preview.hooks.push_back({.name = QStringLiteral("ldconfig"), .conditional = true});
  preview.expires_at = 1700000300;
  return preview;
}

QStringList outArgTypes(const QString& method) {
  QFile file(QString::fromLatin1(HOLONIGHT_HELPER_DBUS_XML));
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  QXmlStreamReader xml(&file);
  QStringList types;
  bool in_method = false;
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement() && xml.name() == QLatin1String("method")) {
      in_method = xml.attributes().value(QLatin1String("name")) == method;
    } else if (xml.isEndElement() && xml.name() == QLatin1String("method")) {
      in_method = false;
    } else if (in_method && xml.isStartElement() && xml.name() == QLatin1String("arg") &&
               xml.attributes().value(QLatin1String("direction")) == QLatin1String("out")) {
      types.push_back(xml.attributes().value(QLatin1String("type")).toString());
    }
  }
  return types;
}

class HelperWire : public testing::Test {
 protected:
  HelperWire() { registerHelperWireTypes(); }
};

TEST_F(HelperWire, MetaTypeSignaturesEqualTheInterfaceXml) {
  EXPECT_EQ(QString::fromLatin1(QDBusMetaType::typeToSignature(QMetaType::fromType<StatusWire>())),
            QStringLiteral("(sasa(ssas))"));
  EXPECT_EQ(outArgTypes(QStringLiteral("PreparePlan")),
            (QStringList{QStringLiteral("(sasa(ssas))"), QStringLiteral("s"),
                         QString::fromLatin1(QDBusMetaType::typeToSignature(QMetaType::fromType<PreviewWire>()))}));
  EXPECT_EQ(outArgTypes(QStringLiteral("Execute")),
            QStringList{QString::fromLatin1(QDBusMetaType::typeToSignature(QMetaType::fromType<ResultWire>()))});
  EXPECT_EQ(outArgTypes(QStringLiteral("Cancel")),
            QStringList{QString::fromLatin1(QDBusMetaType::typeToSignature(QMetaType::fromType<StatusWire>()))});
}

TEST_F(HelperWire, PrepareReplyDecodesTargetsDependenciesOrphansHooksAndExpiry) {
  {
    // In-process values; the demarshalled QDBusArgument form needs a real bus and is covered by the private-bus tests.
    const auto decoded = decodePrepareReply(
        replyWith({QVariant::fromValue(StatusWire{}), QStringLiteral("plan-9"), QVariant::fromValue(installPreview())}),
        "install");
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->plan_id, "plan-9");
    ASSERT_EQ(decoded->plan.changes.size(), 3U);
    EXPECT_EQ(decoded->plan.changes[0].kind, ChangeKind::Target);
    EXPECT_EQ(decoded->plan.changes[0].repository, "extra");
    EXPECT_EQ(decoded->plan.changes[1].kind, ChangeKind::Dependency);
    EXPECT_EQ(decoded->plan.changes[2].kind, ChangeKind::Orphan);
    EXPECT_EQ(decoded->plan.changes[2].old_version, "3-1");
    ASSERT_EQ(decoded->plan.hooks.size(), 1U);
    EXPECT_TRUE(decoded->plan.hooks[0].conditional);
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>(decoded->expires_at.time_since_epoch()).count(),
              1700000300);
  }
}

TEST_F(HelperWire, PrepareRefusalCarriesTheCodeAndNames) {
  StatusWire status;
  status.code = QStringLiteral("PartialUpgradeRequired");
  status.names = {QStringLiteral("glibc")};
  const auto decoded = decodePrepareReply(
      replyWith({QVariant::fromValue(status), QString(), QVariant::fromValue(PreviewWire{})}), "install");
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error().kind, ServiceError::Kind::Refused);
  EXPECT_EQ(decoded.error().failure.code, TransactionErrorCode::PartialUpgradeRequired);
  EXPECT_EQ(decoded.error().failure.names, std::vector<std::string>{"glibc"});
}

TEST_F(HelperWire, AnUnknownStatusCodeBecomesInternalErrorWithNoNames) {
  StatusWire status;
  status.code = QStringLiteral("SomethingElse");
  status.names = {QStringLiteral("secret")};
  const auto decoded = decodePrepareReply(
      replyWith({QVariant::fromValue(status), QString(), QVariant::fromValue(PreviewWire{})}), "install");
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error().failure.code, TransactionErrorCode::InternalError);
  EXPECT_TRUE(decoded.error().failure.names.empty());
}

TEST_F(HelperWire, EveryTaxonomyCodeDecodesToItself) {
  for (std::size_t index = 0; index < kTransactionErrorCodeCount; ++index) {
    const auto code = static_cast<TransactionErrorCode>(index);
    StatusWire status;
    status.code = QString::fromLatin1(transactionErrorName(code).data(),
                                      static_cast<qsizetype>(transactionErrorName(code).size()));
    const auto decoded = decodeCancelReply(replyWith({QVariant::fromValue(status)}));
    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().failure.code, code);
  }
}

TEST_F(HelperWire, PreviewActionMismatchIsAMalformedReply) {
  const auto decoded = decodePrepareReply(
      replyWith({QVariant::fromValue(StatusWire{}), QStringLiteral("p"), QVariant::fromValue(installPreview())}),
      "remove");
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error().kind, ServiceError::Kind::NoReply);
}

TEST_F(HelperWire, WrongArgumentCountOrShapeIsNoReply) {
  EXPECT_EQ(decodePrepareReply(replyWith({}), "install").error().kind, ServiceError::Kind::NoReply);
  EXPECT_EQ(decodeExecuteReply(replyWith({QStringLiteral("not a result")})).error().kind, ServiceError::Kind::NoReply);
  EXPECT_EQ(decodeCancelReply(replyWith({1, 2})).error().kind, ServiceError::Kind::NoReply);
}

TEST_F(HelperWire, UndecodableExecuteReplyMapsToNoReply) {
  QDBusArgument bogus;
  bogus.beginStructure();
  bogus << QStringLiteral("Succeeded");
  bogus.endStructure();
  const auto decoded = decodeExecuteReply(replyWith({QVariant::fromValue(bogus)}));
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error().kind, ServiceError::Kind::NoReply);
}

TEST_F(HelperWire, ExecuteSucceededDecodesChangesAndWarnings) {
  ResultWire wire;
  wire.outcome = QStringLiteral("Succeeded");
  wire.changed.push_back({
      .name = QStringLiteral("app"),
      .action = QStringLiteral("install"),
      .old_version = QString(),
      .new_version = QStringLiteral("1-1"),
  });
  wire.warnings = {QStringLiteral("HookFailed"), QStringLiteral("Mystery")};
  {
    const auto decoded = decodeExecuteReply(replyWith({QVariant::fromValue(wire)}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->outcome, ExecutionOutcome::Succeeded);
    ASSERT_EQ(decoded->changed.size(), 1U);
    EXPECT_EQ(decoded->changed[0].new_version, "1-1");
    EXPECT_EQ(decoded->warnings, std::vector<WarningToken>{WarningToken::HookFailed});
  }
}

TEST_F(HelperWire, ExecuteFailedCarriesTheCodeAndAnEmptyCodeIsInternalError) {
  ResultWire wire;
  wire.outcome = QStringLiteral("Failed");
  wire.status.code = QStringLiteral("DiskSpace");
  auto decoded = decodeExecuteReply(replyWith({QVariant::fromValue(wire)}));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(decoded->failure.code, TransactionErrorCode::DiskSpace);

  wire.status.code.clear();
  decoded = decodeExecuteReply(replyWith({QVariant::fromValue(wire)}));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->failure.code, TransactionErrorCode::InternalError);
}

TEST_F(HelperWire, AnUnknownOutcomeIsAFailedInternalError) {
  ResultWire wire;
  wire.outcome = QStringLiteral("Exploded");
  const auto decoded = decodeExecuteReply(replyWith({QVariant::fromValue(wire)}));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->outcome, ExecutionOutcome::Failed);
  EXPECT_EQ(decoded->failure.code, TransactionErrorCode::InternalError);
}

TEST_F(HelperWire, AnUnparseableChangeMakesTheWholeReplyMalformed) {
  ResultWire wire;
  wire.outcome = QStringLiteral("Succeeded");
  wire.changed.push_back({
      .name = QStringLiteral("app"),
      .action = QStringLiteral("upgrade"),
      .old_version = QString(),
      .new_version = QStringLiteral("1-1"),
  });
  const auto decoded = decodeExecuteReply(replyWith({QVariant::fromValue(wire)}));
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error().kind, ServiceError::Kind::NoReply);
}

TEST_F(HelperWire, CancelSuccessIsAnEmptyCode) {
  EXPECT_TRUE(decodeCancelReply(replyWith({QVariant::fromValue(StatusWire{})})).has_value());
}

TEST_F(HelperWire, ProgressDecodesKnownPhasesAndDropsUnknownOnes) {
  const auto update = decodeProgress(QStringLiteral("Downloading"), 3, 10, QStringLiteral("foo"));
  ASSERT_TRUE(update.has_value());
  EXPECT_EQ(update->phase, ProgressPhase::Downloading);
  EXPECT_EQ(update->current, 3U);
  EXPECT_EQ(update->item, "foo");
  EXPECT_FALSE(decodeProgress(QStringLiteral("Dancing"), 0, 0, QString()).has_value());
}

TEST(HelperErrorClassification, MapsEachBusErrorToItsKind) {
  const auto named = [](const char* name) {
    return QDBusError(QDBusMessage::createError(QString::fromLatin1(name), QStringLiteral("x")));
  };
  EXPECT_EQ(classifyBusError(QDBusError(QDBusError::ServiceUnknown, QStringLiteral("x"))).kind,
            ServiceError::Kind::HelperUnavailable);
  EXPECT_EQ(classifyBusError(named("org.freedesktop.DBus.Error.NameHasNoOwner")).kind,
            ServiceError::Kind::HelperUnavailable);
  EXPECT_EQ(classifyBusError(named("org.freedesktop.DBus.Error.Spawn.ServiceNotFound")).kind,
            ServiceError::Kind::HelperUnavailable);
  EXPECT_EQ(classifyBusError(named("org.freedesktop.DBus.Error.Spawn.ChildExited")).kind,
            ServiceError::Kind::HelperUnavailable);
  EXPECT_EQ(classifyBusError(QDBusError(QDBusError::NoServer, QStringLiteral("x"))).kind,
            ServiceError::Kind::HelperUnavailable);
  for (const auto type : {QDBusError::NoReply, QDBusError::Timeout, QDBusError::TimedOut, QDBusError::Disconnected}) {
    EXPECT_EQ(classifyBusError(QDBusError(type, QStringLiteral("x"))).kind, ServiceError::Kind::NoReply);
  }
  for (const auto type : {QDBusError::AccessDenied, QDBusError::InvalidArgs, QDBusError::UnknownMethod}) {
    EXPECT_EQ(classifyBusError(QDBusError(type, QStringLiteral("x"))).kind, ServiceError::Kind::Rejected);
  }
}

TEST_F(HelperWire, ABusErrorReplyIsClassifiedByTheDecoders) {
  const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("a.b"), QStringLiteral("/x"),
                                                           QStringLiteral("a.b"), QStringLiteral("M"));
  const QDBusMessage error = call.createErrorReply(QDBusError::InvalidArgs, QStringLiteral("bad"));
  EXPECT_EQ(decodePrepareReply(error, "install").error().kind, ServiceError::Kind::Rejected);
  EXPECT_EQ(decodeExecuteReply(error).error().kind, ServiceError::Kind::Rejected);
  EXPECT_EQ(decodeCancelReply(error).error().kind, ServiceError::Kind::Rejected);
}

}  // namespace
