#pragma once

#include "TransactionController.h"
#include "TransactionRowModel.h"

#include <QtQml/qqml.h>

inline void registerTransactionQml() {
  qmlRegisterUncreatableType<TransactionController>("HolonightPackages", 1, 0, "TransactionController",
                                                    QStringLiteral("Provided by the test"));
  qmlRegisterUncreatableType<TransactionRowModel>("HolonightPackages", 1, 0, "TransactionRowModel",
                                                  QStringLiteral("Provided by the test"));
}
