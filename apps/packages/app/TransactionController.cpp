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
using holonight_packages_domain::ProgressPhase;
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
  flow_.setProgressObserver([this] { emit progressChanged(); });
  service_.setCapabilityInvalidatedHandler([this] {
    helper_version_.reset();
    refreshHelper();
    emit changed();
  });
  flow_.setCloseReadyHandler([this] { emit closeReady(); });
  service_.setProgressHandler(
      [this](const std::string& plan_id, const holonight_packages_domain::ProgressUpdate& update) {
        flow_.onProgress(plan_id, update);
      });
}

TransactionController::~TransactionController() {
  // The flow's callbacks held by the service are invalidated by the flow's destructor; stop forwarding progress.
  service_.setProgressHandler({});
  service_.setCapabilityInvalidatedHandler({});
}

void TransactionController::onFlowChanged() {
  syncRows();
  const auto state = flow_.state();
  const bool upgrade_outcome = state != previous_state_ && flow_.isUpgrade() && page() == Page::Result;
  previous_state_ = state;
  if (upgrade_outcome) {
    refreshHelper();
  }
  emit changed();
  emit progressChanged();
}

void TransactionController::syncRows() {
  if (flow_.prepared() && !has_preview_rows_) {
    preview_rows_->setRows(TransactionRowModel::previewRows(flow_.prepared()->plan));
    hook_rows_->setRows(TransactionRowModel::hookRows(flow_.prepared()->plan));
    has_preview_rows_ = true;
  } else if (!flow_.prepared() && has_preview_rows_) {
    preview_rows_->clear();
    hook_rows_->clear();
    has_preview_rows_ = false;
  }
  const FlowState state = flow_.state();
  if (state == previous_state_) {
    return;
  }
  if (state == FlowState::Succeeded || state == FlowState::Failed) {
    result_rows_->setRows(TransactionRowModel::changedRows(flow_.changed()));
  } else if (previous_state_ == FlowState::Succeeded || previous_state_ == FlowState::Failed) {
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
bool TransactionController::allowAction() const { return flow_.allowNewRequest() && !upgrade_probe_pending_; }
bool TransactionController::closable() const { return flow_.closable(); }

QString TransactionController::actionKind() const {
  if (flow_.state() == FlowState::Idle) {
    return {};
  }
  if (flow_.isUpgrade()) {
    return QStringLiteral("upgrade");
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
  if (flow_.isUpgrade()) {
    return messages::upgradeText(messages::UpgradeKey::Title);
  }
  const QString name = packageName();
  return flow_.currentRequest().action == PlanAction::Remove ? tr("Remove %1").arg(name) : tr("Install %1").arg(name);
}

QString TransactionController::message() const {
  const QString keyringNote =
      keyringChanged() ? QStringLiteral(" ") + messages::upgradeText(messages::UpgradeKey::KeyringUpdated) : QString();
  switch (flow_.state()) {
    case FlowState::Refused:
    case FlowState::Failed: {
      if (!flow_.failure()) {
        return {};
      }
      return messages::failureText(flow_.failure()->code, flow_.failure()->names, flow_.currentRequest().action,
                                   flow_.changed().empty()) +
             keyringNote;
    }
    case FlowState::Succeeded:
      if (flow_.isUpgrade()) {
        return messages::upgradeText(messages::UpgradeKey::Succeeded) + keyringNote;
      }
      return messages::noteText(messages::Note::Succeeded);
    case FlowState::Cancelled:
      return messages::noteText(messages::Note::Cancelled);
    case FlowState::Unknown:
      return messages::noteText(messages::Note::Unknown);
    case FlowState::HelperUnavailable:
      return messages::noteText(messages::Note::HelperUnavailable);
    case FlowState::Preparing:
      if (flow_.isUpgrade() && flow_.progress().seen && flow_.progress().phase == ProgressPhase::Syncing) {
        return messages::syncingText(flow_.progress().current, flow_.progress().total, flow_.progress().item);
      }
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
      case FlowAction::UpdateSystem:
        list.push_back(QStringLiteral("updateSystem"));
        break;
    }
  }
  return list;
}

QString TransactionController::confirmLabel() const {
  if (flow_.isUpgrade()) {
    return messages::upgradeText(messages::UpgradeKey::ConfirmLabel);
  }
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

bool TransactionController::cancelEnabled() const { return flow_.cancelEnabled() || flow_.preparationCancelEnabled(); }

QString TransactionController::cancelLabel() const {
  return flow_.cancelling() ? messages::noteText(messages::Note::Cancelling) : tr("Cancel");
}

bool TransactionController::requestInstall(const QString& name) {
  return allowAction() && flow_.request(PlanRequest{.action = PlanAction::Install, .names = {name.toStdString()}});
}

bool TransactionController::requestRemove(const QString& name) {
  return allowAction() && flow_.request(PlanRequest{.action = PlanAction::Remove, .names = {name.toStdString()}});
}

void TransactionController::confirm() { flow_.confirm(); }
void TransactionController::dismiss() { flow_.dismiss(); }
void TransactionController::cancelRun() {
  if (flow_.state() == FlowState::Preparing) {
    flow_.cancelPreparation();
    return;
  }
  flow_.cancelRun();
}
void TransactionController::tryAgain() {
  if (!actions().contains(QStringLiteral("tryAgain")) || upgrade_probe_pending_) {
    return;
  }
  if (isUpgrade()) {
    probeUpgrade(UpgradeEntry::Retry);
  } else {
    flow_.tryAgain();
  }
}

bool TransactionController::isUpgrade() const { return flow_.state() != FlowState::Idle && flow_.isUpgrade(); }

bool TransactionController::helperTooOld() const { return helper_version_ && *helper_version_ < 2; }

bool TransactionController::upgradeEnabled() const {
  return flow_.allowNewRequest() && !helperTooOld() && !upgrade_probe_pending_;
}

QString TransactionController::upgradeDisabledReason() const {
  return helperTooOld() ? messages::upgradeText(messages::UpgradeKey::HelperTooOld) : QString();
}

QString TransactionController::stepText() const {
  const auto& progress = flow_.progress();
  if (!flow_.isUpgrade() || progress.step_count == 0) {
    return {};
  }
  return messages::stepText(progress.step_index, progress.step_count);
}

QString TransactionController::keyringNotice() const {
  if (!flow_.prepared() || !flow_.prepared()->plan.upgrade || !flow_.prepared()->plan.upgrade->keyring()) {
    return {};
  }
  QStringList names;
  for (const std::string& name : flow_.prepared()->plan.upgrade->keyring_packages) {
    names.push_back(QString::fromStdString(name));
  }
  return messages::upgradeText(messages::UpgradeKey::Keyring).arg(names.join(QStringLiteral(", ")));
}

QString TransactionController::downloadText() const {
  if (!flow_.prepared() || !flow_.prepared()->plan.upgrade) {
    return {};
  }
  return tr("Download: %1")
      .arg(QLocale::system().formattedDataSize(static_cast<qint64>(flow_.prepared()->plan.upgrade->download_bytes)));
}

QString TransactionController::sizeChangeText() const {
  if (!flow_.prepared() || !flow_.prepared()->plan.upgrade) {
    return {};
  }
  const std::int64_t change = flow_.prepared()->plan.upgrade->size_change_bytes;
  const QString size = QLocale::system().formattedDataSize(change < 0 ? -change : change);
  return change < 0 ? tr("Installed size: %1 less").arg(size) : tr("Installed size: %1 more").arg(size);
}

QString TransactionController::previewHeading() const {
  return isUpgrade() ? messages::upgradeText(messages::UpgradeKey::Preview) : QString();
}

QString TransactionController::heldBackHeading() { return messages::upgradeText(messages::UpgradeKey::HeldBack); }

bool TransactionController::keyringChanged() const {
  if (!flow_.isUpgrade()) {
    return false;
  }
  return std::ranges::any_of(flow_.changed(), [](const holonight_packages_domain::ChangedPackage& change) {
    return change.name == "archlinux-keyring" && change.action == holonight_packages_domain::ChangeAction::Install;
  });
}

void TransactionController::refreshHelper() {
  if (!helper_observed_ || upgrade_probe_pending_) {
    return;
  }
  const auto generation = ++passive_probe_generation_;
  const std::weak_ptr<int> alive = alive_;
  service_.queryProtocolVersion(holonight_packages_domain::ProbeMode::NoAutoStart,
                                [this, alive, generation](holonight_packages_domain::VersionReply reply) {
                                  if (alive.expired() || generation != passive_probe_generation_) {
                                    return;
                                  }
                                  helper_version_ = reply ? std::optional{*reply} : std::nullopt;
                                  emit changed();
                                });
}

void TransactionController::probeHelper() {
  if (!std::exchange(helper_observed_, true)) {
    refreshHelper();
  }
}

void TransactionController::probeUpgrade(UpgradeEntry entry) {
  using holonight_packages_domain::ServiceError;
  ++passive_probe_generation_;
  upgrade_probe_pending_ = true;
  const std::weak_ptr<int> alive = alive_;
  const auto state = flow_.state();
  service_.queryProtocolVersion(
      holonight_packages_domain::ProbeMode::AutoStart,
      [this, alive, entry, state](holonight_packages_domain::VersionReply reply) {
        if (alive.expired()) {
          return;
        }
        upgrade_probe_pending_ = false;
        helper_version_ = reply ? std::optional{*reply} : std::nullopt;
        if (flow_.state() == state && !helperTooOld()) {
          const auto error =
              reply ? std::nullopt
                    : std::optional{ServiceError{.kind = ServiceError::Kind::HelperUnavailable, .failure = {}}};
          if (entry == UpgradeEntry::Retry) {
            flow_.tryAgain(error);
          } else {
            flow_.requestUpgrade(error);
          }
        }
        emit changed();
      });
  emit changed();
}

bool TransactionController::requestUpgrade() {
  if (!upgradeEnabled()) {
    return false;
  }
  probeUpgrade(UpgradeEntry::Page);
  return true;
}

void TransactionController::updateSystem() {
  if (!upgrade_probe_pending_ && actions().contains(QStringLiteral("updateSystem"))) {
    probeUpgrade(UpgradeEntry::PartialUpgrade);
  }
}

holonight_packages_application::CloseDecision TransactionController::handleCloseRequest() {
  return flow_.closeRequested();
}
