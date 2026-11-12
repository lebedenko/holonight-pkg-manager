#include "question_policy.h"

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::NeedsDecisionChoice;

std::string nameOf(alpm_pkg_t* package) { return package != nullptr ? alpm_pkg_get_name(package) : std::string(); }

}  // namespace

void QuestionPolicy::clear() {
  decisions_.clear();
  unresolvable_.clear();
  ignored_.clear();
  corrupted_.clear();
  asked_.clear();
  corruptedReason_ = static_cast<alpm_errno_t>(0);
  keyImportRefused_ = false;
  replacements_.clear();
}

void QuestionPolicy::handle(alpm_question_t* question) {
  switch (question->any.type) {
    case ALPM_QUESTION_INSTALL_IGNOREPKG:
      asked_.emplace_back("install-ignored-package");
      question->install_ignorepkg.install = 0;
      ignored_.push_back(nameOf(question->install_ignorepkg.pkg));
      break;
    case ALPM_QUESTION_REPLACE_PKG:
      asked_.emplace_back("replace-package");
      if (upgradeMode_) {
        question->replace.replace = 1;
        replacements_.emplace_back(nameOf(question->replace.oldpkg), nameOf(question->replace.newpkg));
        break;
      }
      question->replace.replace = 0;
      decisions_.push_back(NeedsDecisionChoice{
          .kind = NeedsDecisionChoice::Kind::Replace,
          .subject = nameOf(question->replace.oldpkg),
          .candidates = {nameOf(question->replace.oldpkg), nameOf(question->replace.newpkg)},
      });
      break;
    case ALPM_QUESTION_CONFLICT_PKG: {
      asked_.emplace_back("conflicting-package");
      question->conflict.remove = 0;
      const alpm_conflict_t* conflict = question->conflict.conflict;
      decisions_.push_back(NeedsDecisionChoice{
          .kind = NeedsDecisionChoice::Kind::Conflict,
          .subject = nameOf(conflict->package1),
          .candidates = {nameOf(conflict->package1), nameOf(conflict->package2)},
      });
      break;
    }
    case ALPM_QUESTION_REMOVE_PKGS:
      asked_.emplace_back("skip-unresolvable");
      question->remove_pkgs.skip = 0;
      for (const alpm_list_t* item = question->remove_pkgs.packages; item != nullptr; item = item->next) {
        unresolvable_.push_back(nameOf(static_cast<alpm_pkg_t*>(item->data)));
      }
      break;
    case ALPM_QUESTION_SELECT_PROVIDER: {
      asked_.emplace_back("select-provider");
      question->select_provider.use_index = 0;
      NeedsDecisionChoice choice{.kind = NeedsDecisionChoice::Kind::Provider, .subject = {}, .candidates = {}};
      if (question->select_provider.depend != nullptr && question->select_provider.depend->name != nullptr) {
        choice.subject = question->select_provider.depend->name;
      }
      for (const alpm_list_t* item = question->select_provider.providers; item != nullptr; item = item->next) {
        choice.candidates.push_back(nameOf(static_cast<alpm_pkg_t*>(item->data)));
      }
      decisions_.push_back(std::move(choice));
      break;
    }
    case ALPM_QUESTION_CORRUPTED_PKG:
      asked_.emplace_back("corrupted-package");
      // Only a damaged file is deleted so that a retry downloads it again; a signature that does not verify is not
      // corruption and is never retried (spike S-6).
      question->corrupted.remove = corruptedRemove_ && question->corrupted.reason != ALPM_ERR_PKG_INVALID_SIG ? 1 : 0;
      corrupted_.emplace_back(question->corrupted.filepath != nullptr ? question->corrupted.filepath : "");
      corruptedReason_ = question->corrupted.reason;
      break;
    case ALPM_QUESTION_IMPORT_KEY:
      asked_.emplace_back("import-key");
      question->import_key.import = 0;
      keyImportRefused_ = true;
      break;
    default:
      asked_.emplace_back("unknown");
      question->any.answer = 0;
      break;
  }
}

}  // namespace holonight_packages_transaction
