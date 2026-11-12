find_package(PkgConfig REQUIRED)
pkg_check_modules(Alpm REQUIRED IMPORTED_TARGET libalpm)

# The privileged transaction core. It links no Qt, D-Bus or polkit code (REQ-C-001, REQ-C-002).
add_library(holonight_packages_transaction STATIC
  include/holonight_packages_transaction/alpm_transaction_engine.h
  include/holonight_packages_transaction/database_lock.h
  include/holonight_packages_transaction/log_sink.h
  include/holonight_packages_transaction/transaction_config.h
  include/holonight_packages_transaction/transaction_seams.h
  src/alpm_session.h
  src/alpm_session.cpp
  src/alpm_transaction_engine.cpp
  src/commit_gate.h
  src/commit_gate.cpp
  src/download_classifier.h
  src/download_classifier.cpp
  src/error_mapping.h
  src/error_mapping.cpp
  src/event_mapper.h
  src/event_mapper.cpp
  src/execution_driver.h
  src/execution_driver.cpp
  src/failure_reporter.cpp
  src/guard_snapshot.cpp
  src/promotion.cpp
  src/scratch_dbpath.cpp
  src/sync_refresher.cpp
  src/upgrade_driver.cpp
  src/upgrade_resolver.cpp
  src/hook_preview.h
  src/hook_preview.cpp
  src/log_sink.cpp
  src/plan_resolver.h
  src/plan_resolver.cpp
  src/protected_set.h
  src/protected_set.cpp
  src/question_policy.h
  src/question_policy.cpp
  src/signature_policy.h
  src/signature_policy.cpp
  src/version_policy.h
  src/version_policy.cpp
)
target_include_directories(holonight_packages_transaction
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(holonight_packages_transaction
  PUBLIC holonight_packages_domain
  PRIVATE PkgConfig::Alpm holonight_packages_pacman_config)
target_compile_features(holonight_packages_transaction PUBLIC cxx_std_23)
