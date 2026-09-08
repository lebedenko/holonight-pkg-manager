#include <QDir>
#include <QGuiApplication>
#include <QTemporaryDir>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
  QTemporaryDir isolation;
  if (!isolation.isValid()) {
    return 1;
  }
  qputenv("HOME", isolation.path().toUtf8());
  for (const auto* name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_CONFIG_DIRS", "XDG_DATA_DIRS"}) {
    const auto path = isolation.path() + QLatin1Char('/') + QString::fromLatin1(name);
    if (!QDir{}.mkpath(path)) {
      return 1;
    }
    qputenv(name, path.toUtf8());
  }
  qputenv("XDG_RUNTIME_DIR", isolation.path().toUtf8());
  qputenv("QT_QUICK_BACKEND", "software");
  qputenv("QT_FORCE_STDERR_LOGGING", "1");
  qputenv("QT_QPA_PLATFORMTHEME", "");
  qunsetenv("QT_STYLE_OVERRIDE");
  qunsetenv("QT_QUICK_CONTROLS_CONF");
  qunsetenv("HOLONIGHT_APPEARANCE_FILE");
  qputenv("QML_IMPORT_PATH", HOLONIGHT_QML_IMPORT_PATH);
  qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/uqc105-bus");
  QGuiApplication app(argc, argv);
  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
