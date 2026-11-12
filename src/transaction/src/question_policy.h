#pragma once

#include "holonight_packages_domain/transaction_error.h"

#include <alpm.h>
#include <string>
#include <utility>
#include <vector>

namespace holonight_packages_transaction {

// Answers libalpm questions without ever blocking or asking a caller (REQ-F-053, REQ-F-054). It records what libalpm
// wanted decided so the resolver and the executor can report it:
//   provider / replace / conflict   -> answered "no / first", recorded as a NeedsDecision choice
//   skip unresolvable packages      -> answered "no", recorded
//   install an ignored package      -> answered "no", recorded
//   corrupted package               -> answered per setCorruptedAnswer(), recorded
//   import a key                    -> always "no"
// In upgrade mode (system-upgrade REQ-F-020, REQ-F-030) a replacement is accepted, as pacman's default answer is,
// and recorded as a replacement instead of a decision. Everything else is answered as above.
class QuestionPolicy {
 public:
  void handle(alpm_question_t* question);

  void setUpgradeMode(bool upgrade) { upgradeMode_ = upgrade; }
  // Replacements accepted in upgrade mode: (installed package, replacing package).
  [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& acceptedReplacements() const {
    return replacements_;
  }

  // Whether the next corrupted-package question deletes the cached file (so a retry downloads it again).
  void setCorruptedAnswer(bool remove) { corruptedRemove_ = remove; }

  void clear();

  [[nodiscard]] const std::vector<holonight_packages_domain::NeedsDecisionChoice>& decisions() const {
    return decisions_;
  }
  [[nodiscard]] const std::vector<std::string>& unresolvable() const { return unresolvable_; }
  [[nodiscard]] const std::vector<std::string>& ignoredTargets() const { return ignored_; }
  [[nodiscard]] const std::vector<std::string>& corruptedFiles() const { return corrupted_; }
  [[nodiscard]] alpm_errno_t lastCorruptedReason() const { return corruptedReason_; }
  [[nodiscard]] bool keyImportRefused() const { return keyImportRefused_; }
  // One entry per question, the question type only (no libalpm text).
  [[nodiscard]] const std::vector<std::string>& askedTypes() const { return asked_; }

 private:
  std::vector<holonight_packages_domain::NeedsDecisionChoice> decisions_;
  std::vector<std::string> unresolvable_;
  std::vector<std::string> ignored_;
  std::vector<std::string> corrupted_;
  std::vector<std::string> asked_;
  alpm_errno_t corruptedReason_ = static_cast<alpm_errno_t>(0);
  bool corruptedRemove_ = true;
  bool keyImportRefused_ = false;
  bool upgradeMode_ = false;
  std::vector<std::pair<std::string, std::string>> replacements_;
};

}  // namespace holonight_packages_transaction
