#include "TransactionController.h"

#include "TransactionMessages.h"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>

namespace {

using holonight_packages_application::FlowAction;
using holonight_packages_application::FlowNotice;
using holonight_packages_application::FlowState;
using holonight_packages_domain::PlanAction;
using holonight_packages_domain::PlanRequest;
using holonight_packages_domain::WarningToken;
namespace messages = transaction_messages;

QString tr(const char* source) { return QCoreApplication::translate("TransactionController", source); }

}  // namespace

static_assert(static_cast<int>(FlowState::HelperUnavailable) ==
              static_cast<int>(TransactionController::State::HelperUnavailable));

TransactionController::TransactionController(holonight_packages_domain::TransactionService& service,
                                             holonight_packages_application::PackageListRefresh& refresh,
                                             QObject* parent)
    : QObject(parent),
      service_(service),
      flow_(service, refresh),
      preview_rows_(new TransactionRowModel(this)),
      hook_rows_(new TransactionRowModel(this)),
      result_rows_(new TransactionRowModel(this)) {
  flow_.setObserver([this] { onFlowChanged(); });
  flow_.setCloseReadyHandler([this] { emit closeReady(); });
  service_.setProgressHandler(
      [this](const std::string& plan_id, const holonight_packages_domain::ProgressUpdate& update) {
        flow_.onProgress(plan_id, update);
      });
}

TransactionController::~TransactionController() {
  // The flow's callbacks held by the service are invalidated by the flow's destructor; stop forwarding progress.
  service_.setProgressHandler({});
}

void TransactionController::onFlowChanged() {
  syncRows();
  emit changed();
}

void TransactionController::syncRows() {
  if (flow_.prepared()) {
    preview_rows_->setRows(TransactionRowModel::previewRows(flow_.prepared()->plan));
    hook_rows_->setRows(TransactionRowModel::hookRows(flow_.prepared()->plan));
  } else {
    preview_rows_->clear();
    hook_rows_->clear();
  }
  const FlowState state = flow_.state();
  if (state == FlowState::Succeeded || state == FlowState::Failed) {
    result_rows_->setRows(TransactionRowModel::changedRows(flow_.changed()));
  } else {
    result_rows_->clear();
  }
}

TransactionController::State TransactionController::state() const { return static_cast<State>(flow_.state()); }

TransactionController::Page TransactionController::page() const {
  switch (flow_.state()) {
    case FlowState::Idle:
      return Page::None;
    case FlowState::Preparing:
      return Page::Preparing;
    case FlowState::Previewed:
      return Page::Confirm;
    case FlowState::Authorizing:
    case FlowState::Executing:
      return Page::Progress;
    case FlowState::Refused:
    case FlowState::Succeeded:
    case FlowState::Failed:
    case FlowState::Cancelled:
    case FlowState::Unknown:
    case FlowState::HelperUnavailable:
      return Page::Result;
  }
  return Page::None;
}

bool TransactionController::dialogOpen() const { return flow_.state() != FlowState::Idle; }
int TransactionController::openDialogCount() const { return flow_.openDialogCount(); }
bool TransactionController::allowAction() const { return flow_.allowNewRequest(); }
bool TransactionController::closable() const { return flow_.closable(); }

QString TransactionController::actionKind() const {
  if (flow_.state() == FlowState::Idle) {
    return {};
  }
  return flow_.currentRequest().action == PlanAction::Remove ? QStringLiteral("remove") : QStringLiteral("install");
}

QString TransactionController::packageName() const {
  const auto& names = flow_.currentRequest().names;
  return flow_.state() == FlowState::Idle || names.empty() ? QString() : QString::fromStdString(names.front());
}

QString TransactionController::title() const {
  if (flow_.state() == FlowState::Idle) {
    return {};
  }
  const QString name = packageName();
  return flow_.currentRequest().action == PlanAction::Remove ? tr("Remove %1").arg(name) : tr("Install %1").arg(name);
}

QString TransactionController::message() const {
  switch (flow_.state()) {
    case FlowState::Refused:
    case FlowState::Failed: {
      if (!flow_.failure()) {
        return {};
      }
      return messages::failureText(flow_.failure()->code, flow_.failure()->names, flow_.currentRequest().action,
                                   flow_.changed().empty());
    }
    case FlowState::Succeeded:
      return messages::noteText(messages::Note::Succeeded);
    case FlowState::Cancelled:
      return messages::noteText(messages::Note::Cancelled);
    case FlowState::Unknown:
      return messages::noteText(messages::Note::Unknown);
    case FlowState::HelperUnavailable:
      return messages::noteText(messages::Note::HelperUnavailable);
    case FlowState::Preparing:
      return messages::noteText(messages::Note::Preparing);
    default:
      return {};
  }
}

QString TransactionController::noticeText() const {
  switch (flow_.notice()) {
    case FlowNotice::NotAuthorized:
      return messages::noteText(messages::Note::NotAuthorized);
    case FlowNotice::CloseUnavailable:
      return messages::noteText(messages::Note::CloseUnavailable);
    case FlowNotice::None:
      break;
  }
  return hasHookWarning() ? messages::noteText(messages::Note::HookWarning) : QString();
}

QStringList TransactionController::actions() const {
  QStringList list;
  for (const FlowAction action : flow_.actions()) {
    switch (action) {
      case FlowAction::Confirm:
        list.push_back(QStringLiteral("confirm"));
        break;
      case FlowAction::Cancel:
        list.push_back(QStringLiteral("cancel"));
        break;
      case FlowAction::TryAgain:
        list.push_back(QStringLiteral("tryAgain"));
        break;
      case FlowAction::Close:
        list.push_back(QStringLiteral("close"));
        break;
    }
  }
  return list;
}

QString TransactionController::confirmLabel() const {
  if (flow_.currentRequest().action == PlanAction::Remove) {
    const int count = flow_.prepared() ? static_cast<int>(flow_.prepared()->plan.changes.size()) : 1;
    return messages::removeConfirmLabel(std::max(count, 1));
  }
  return tr("Install");
}

bool TransactionController::confirmEnabled() const { return flow_.state() == FlowState::Previewed; }

QDateTime TransactionController::expiresAt() const {
  if (!flow_.prepared()) {
    return {};
  }
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(flow_.prepared()->expires_at.time_since_epoch());
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds.count()));
}

QString TransactionController::expiresText() const {
  if (!flow_.prepared()) {
    return {};
  }
  return tr("This confirmation expires at %1.")
      .arg(QLocale::system().toString(expiresAt().time(), QLocale::ShortFormat));
}

bool TransactionController::hasHookWarning() const {
  const auto& warnings = flow_.warnings();
  return flow_.state() == FlowState::Succeeded &&
         std::ranges::find(warnings, WarningToken::HookFailed) != warnings.end();
}

QString TransactionController::phaseLabel() const {
  if (flow_.state() != FlowState::Authorizing && flow_.state() != FlowState::Executing) {
    return {};
  }
  if (!flow_.progress().seen) {
    return messages::noteText(messages::Note::Authorizing);
  }
  return messages::phaseLabel(flow_.progress().phase);
}

QString TransactionController::currentItem() const { return QString::fromStdString(flow_.progress().item); }

qreal TransactionController::progressValue() const {
  const auto& progress = flow_.progress();
  if (progress.total == 0) {
    return 0.0;
  }
  return std::clamp(static_cast<qreal>(progress.current) / static_cast<qreal>(progress.total), 0.0, 1.0);
}

bool TransactionController::progressIndeterminate() const { return flow_.progress().total == 0; }

bool TransactionController::cancelEnabled() const { return flow_.cancelEnabled(); }

QString TransactionController::cancelLabel() const {
  return flow_.cancelling() ? messages::noteText(messages::Note::Cancelling) : tr("Cancel");
}

bool TransactionController::requestInstall(const QString& name) {
  return flow_.request(PlanRequest{.action = PlanAction::Install, .names = {name.toStdString()}});
}

bool TransactionController::requestRemove(const QString& name) {
  return flow_.request(PlanRequest{.action = PlanAction::Remove, .names = {name.toStdString()}});
}

void TransactionController::confirm() { flow_.confirm(); }
void TransactionController::dismiss() { flow_.dismiss(); }
void TransactionController::cancelRun() { flow_.cancelRun(); }
void TransactionController::tryAgain() { flow_.tryAgain(); }

holonight_packages_application::CloseDecision TransactionController::handleCloseRequest() {
  return flow_.closeRequested();
}
