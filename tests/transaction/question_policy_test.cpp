#include "question_policy.h"

#include <array>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace holonight_packages_transaction {
namespace {

using holonight_packages_domain::NeedsDecisionChoice;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

// libalpm's question structs, filled by hand. The packages are null: names then come out empty, which is enough to see
// the answers and the recorded kinds.
alpm_question_t question(alpm_question_type_t type) {
  alpm_question_t value{};
  value.any.type = type;
  return value;
}

TEST(QuestionPolicyTest, ImportKeyIsAlwaysRefusedAndRemembered) {
  QuestionPolicy policy;
  alpm_question_t import = question(ALPM_QUESTION_IMPORT_KEY);
  import.import_key.import = 1;
  policy.handle(&import);
  EXPECT_EQ(import.import_key.import, 0);
  EXPECT_TRUE(policy.keyImportRefused());
  EXPECT_THAT(policy.askedTypes(), ElementsAre("import-key"));
  EXPECT_THAT(policy.decisions(), IsEmpty());  // a refused key is not a decision for the caller
}

TEST(QuestionPolicyTest, DecisionQuestionsGetTheNonActingAnswerAndAreRecorded) {
  QuestionPolicy policy;
  alpm_question_t replace = question(ALPM_QUESTION_REPLACE_PKG);
  replace.replace.replace = 1;
  policy.handle(&replace);
  EXPECT_EQ(replace.replace.replace, 0);

  alpm_conflict_t conflict{};
  alpm_question_t conflicting = question(ALPM_QUESTION_CONFLICT_PKG);
  conflicting.conflict.remove = 1;
  conflicting.conflict.conflict = &conflict;
  policy.handle(&conflicting);
  EXPECT_EQ(conflicting.conflict.remove, 0);

  std::array<char, 7> editor{'e', 'd', 'i', 't', 'o', 'r', '\0'};
  alpm_depend_t depend{};
  depend.name = editor.data();
  alpm_question_t provider = question(ALPM_QUESTION_SELECT_PROVIDER);
  provider.select_provider.use_index = 3;
  provider.select_provider.depend = &depend;
  policy.handle(&provider);
  EXPECT_EQ(provider.select_provider.use_index, 0);

  ASSERT_EQ(policy.decisions().size(), 3U);
  EXPECT_EQ(policy.decisions()[0].kind, NeedsDecisionChoice::Kind::Replace);
  EXPECT_EQ(policy.decisions()[1].kind, NeedsDecisionChoice::Kind::Conflict);
  EXPECT_EQ(policy.decisions()[2].kind, NeedsDecisionChoice::Kind::Provider);
  EXPECT_EQ(policy.decisions()[2].subject, "editor");
  EXPECT_THAT(policy.askedTypes(), ElementsAre("replace-package", "conflicting-package", "select-provider"));
}

TEST(QuestionPolicyTest, IgnoredAndUnresolvablePackagesAreRefusedAndRecorded) {
  QuestionPolicy policy;
  alpm_question_t ignored = question(ALPM_QUESTION_INSTALL_IGNOREPKG);
  ignored.install_ignorepkg.install = 1;
  policy.handle(&ignored);
  EXPECT_EQ(ignored.install_ignorepkg.install, 0);
  alpm_question_t skip = question(ALPM_QUESTION_REMOVE_PKGS);
  skip.remove_pkgs.skip = 1;
  policy.handle(&skip);
  EXPECT_EQ(skip.remove_pkgs.skip, 0);
  EXPECT_THAT(policy.askedTypes(), ElementsAre("install-ignored-package", "skip-unresolvable"));
}

TEST(QuestionPolicyTest, ACorruptedPackageIsDeletedOnlyWhenAskedTo) {
  QuestionPolicy policy;
  alpm_question_t corrupted = question(ALPM_QUESTION_CORRUPTED_PKG);
  corrupted.corrupted.filepath = "/cache/app-1.0-1-any.pkg.tar.zst";
  corrupted.corrupted.reason = ALPM_ERR_PKG_INVALID_CHECKSUM;
  policy.handle(&corrupted);
  EXPECT_EQ(corrupted.corrupted.remove, 1);
  EXPECT_THAT(policy.corruptedFiles(), ElementsAre("/cache/app-1.0-1-any.pkg.tar.zst"));
  EXPECT_EQ(policy.lastCorruptedReason(), ALPM_ERR_PKG_INVALID_CHECKSUM);
  policy.setCorruptedAnswer(false);
  policy.handle(&corrupted);
  EXPECT_EQ(corrupted.corrupted.remove, 0);
}

TEST(QuestionPolicyTest, ClearForgetsEverything) {
  QuestionPolicy policy;
  alpm_question_t import = question(ALPM_QUESTION_IMPORT_KEY);
  policy.handle(&import);
  policy.clear();
  EXPECT_FALSE(policy.keyImportRefused());
  EXPECT_THAT(policy.askedTypes(), IsEmpty());
}

}  // namespace
}  // namespace holonight_packages_transaction
