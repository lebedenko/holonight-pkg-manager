#include "root_guard.h"

#include <QCoreApplication>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <span>

int main(int argc, char* argv[]) {
  if (const int refused =
          holonight_test_support::refuseToRunAsRoot(std::span<char*>(argv, static_cast<std::size_t>(argc)).front());
      refused != 0) {
    return refused;
  }
  QCoreApplication app(argc, argv);
  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
