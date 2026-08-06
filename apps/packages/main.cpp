#include "PackagesApplication.h"

#include <QGuiApplication>

int main(int argc, char* argv[]) {
  PackagesApplication app(argc, argv);
  return QGuiApplication::exec();
}
