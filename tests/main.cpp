#include "UpdateCheckModel.h"
#include "UpdateStatusPresentation.h"
#include "register_transaction_qml.h"

#include <QByteArray>
#include <QGuiApplication>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  qputenv("QML_IMPORT_PATH", QByteArray(HOLONIGHT_QML_IMPORT_PATH));
  QGuiApplication app(argc, argv);
  registerTransactionQml();
  qmlRegisterUncreatableType<UpdateCheckModel>("HolonightPackages", 1, 0, "UpdateCheckModel",
                                               QStringLiteral("Provided by the test"));
  qmlRegisterUncreatableType<UpdateStatusPresentation>("HolonightPackages", 1, 0, "UpdateStatusPresentation",
                                                       QStringLiteral("Provided by the test"));
  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
