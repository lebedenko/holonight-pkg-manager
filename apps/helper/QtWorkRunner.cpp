#include "QtWorkRunner.h"

#include <QMetaObject>
#include <QThreadPool>

namespace holonight_helper {

void QtWorkRunner::run(std::function<void()> work, std::function<void()> done) {
  QThreadPool::globalInstance()->start([this, work = std::move(work), done = std::move(done)] mutable {
    work();
    QMetaObject::invokeMethod(this, std::move(done), Qt::QueuedConnection);
  });
}

}  // namespace holonight_helper
