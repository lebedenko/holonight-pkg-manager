#include "PackagesApplication.h"

#include <QGuiApplication>

#include <cstdlib>

int main(int argc, char* argv[]) {
  PackagesApplication app(argc, argv);
  if (!app.isReady()) {
    return EXIT_FAILURE;
  }
  return QGuiApplication::exec();
}
