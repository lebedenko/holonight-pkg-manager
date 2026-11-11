include(GoogleTest)

# Libalpm-free, Qt Quick-free checks executable (REQ-NF-007). Keep it fast: every test has a 10 s TIMEOUT.
add_executable(test_holonight_packages_checks
  checks_main.cpp
  domain/update_checker_test.cpp
  domain/transaction_error_test.cpp
  domain/transaction_plan_test.cpp
  domain/transaction_request_test.cpp
  domain/transaction_service_test.cpp
  application/fake_clock_test.cpp
  application/transaction_ports_test.cpp
  application/plan_store_test.cpp
  application/plan_service_test.cpp
  application/transaction_doubles_test.cpp
  application/transaction_flow_test.cpp
  application/idle_monitor_test.cpp
  application/snapshot_selection_test.cpp
  application/update_age_format_test.cpp
  application/update_monitor_adopt_test.cpp
  persistence/json_update_snapshot_store_test.cpp
  persistence/check_history_test.cpp
  application/update_check_policy_test.cpp
  application/update_check_scheduler_test.cpp
  application/update_check_service_test.cpp
)
target_include_directories(test_holonight_packages_checks PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/application
  ${CMAKE_CURRENT_SOURCE_DIR}/support
  ${CMAKE_CURRENT_SOURCE_DIR}/apps)
set_target_properties(test_holonight_packages_checks PROPERTIES AUTOMOC ON)
target_compile_features(test_holonight_packages_checks PRIVATE cxx_std_23)
target_link_libraries(test_holonight_packages_checks PRIVATE
  GTest::gtest GTest::gmock Qt6::Core Qt6::Test Qt6::Concurrent
  holonight_packages_application
  holonight_packages_snapshot_store
  holonight_packages_domain
)
gtest_discover_tests(test_holonight_packages_checks
  TEST_PREFIX "holonight_packages_checks."
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 10 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME holonight_packages_checks_no_qtdbus
  COMMAND sh -c "! ldd \"$1\" | grep -q libQt6DBus" sh $<TARGET_FILE:test_holonight_packages_checks>)
add_test(NAME holonight_packages_checks_no_libalpm
  COMMAND sh -c "! ldd \"$1\" | grep -q libalpm" sh $<TARGET_FILE:test_holonight_packages_checks>)

add_executable(test_holonight_packages
  main.cpp
  test_placeholder.cpp
  domain/package_test.cpp
  backends/alpm_error_mapping_test.cpp
  backends/alpm_explore_source_test.cpp
  backends/alpm_package_source_test.cpp
  backends/alpm_update_checker_test.cpp
  backends/alpm_update_source_test.cpp
  backends/repository_catalog_test.cpp
  backends/file_repo_fixture_test.cpp
  backends/pacman_config_test.cpp
  backends/scratch_dir_test.cpp
  backends/pacman_repositories_test.cpp
  backends/pacman_settings_test.cpp
  backends/pending_update_computation_test.cpp
  backends/sync_database_files_test.cpp
  backends/update_matching_test.cpp
  persistence/alpm_connection_cache_test.cpp
  application/explore_search_test.cpp
  application/package_list_use_case_test.cpp
  application/orphan_package_filter_test.cpp
  application/package_size_formatter_test.cpp
  application/update_monitor_integration_test.cpp
  application/update_monitor_test.cpp
  application/update_status_test.cpp
  application/update_summary_test.cpp
  packaged/packaged_config_test.cpp
  packaged/packaged_dbus_test.cpp
  packaged/packaged_process_test.cpp
  packaged/packaged_update_check_dbus_test.cpp
  packaged/update_status_client_test.cpp
  application/data_freshness_test.cpp
  apps/installed_packages_model_test.cpp
  apps/installed_packages_filter_model_test.cpp
  apps/installed_packages_view_test.cpp
  apps/package_icon_resolver_test.cpp
  apps/explore_model_test.cpp
  apps/helper_wire_test.cpp
  apps/transaction_messages_test.cpp
  apps/model_list_refresh_test.cpp
  apps/explore_view_test.cpp
  apps/snapshot_file_reader_test.cpp
  apps/update_check_client_test.cpp
  apps/update_check_model_test.cpp
  apps/updates_model_checked_snapshot_test.cpp
  apps/updates_model_test.cpp
  apps/updates_view_test.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesFilterModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdatesModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/ExploreModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/ModelListRefresh.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/HelperWire.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/TransactionMessages.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/DataFreshness.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdateStatusClient.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdateCheckClient.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/DBusUpdateCheckClient.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/SnapshotFileReader.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdateCheckModel.cpp
)
qt_add_resources(test_holonight_packages workspace_assets
  PREFIX "/HolonightPackages"
  BASE "${PROJECT_SOURCE_DIR}"
  FILES "${PROJECT_SOURCE_DIR}/assets/holonight-pkg-manager.svg")

target_include_directories(test_holonight_packages PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/application
  ${CMAKE_CURRENT_SOURCE_DIR}/apps
  ${CMAKE_CURRENT_SOURCE_DIR}/backends
  ${CMAKE_CURRENT_SOURCE_DIR}/support
  ${CMAKE_CURRENT_SOURCE_DIR}/packaged
  ${PROJECT_SOURCE_DIR}/apps/packages/app
  ${PROJECT_SOURCE_DIR}/src/backends/src)
set_target_properties(test_holonight_packages PROPERTIES AUTOMOC ON)
add_dependencies(test_holonight_packages holonight-packaged)
target_compile_features(test_holonight_packages PRIVATE cxx_std_23)
target_compile_definitions(test_holonight_packages PRIVATE
  HOLONIGHT_QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH}"
  HOLONIGHT_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  HOLONIGHT_PACKAGED_EXECUTABLE="$<TARGET_FILE:holonight-packaged>"
  HOLONIGHT_HELPER_DBUS_XML="${PROJECT_SOURCE_DIR}/apps/helper/dbus/org.holonight.PackageHelper1.xml"
  HOLONIGHT_QML_SOURCE_DIR="${PROJECT_SOURCE_DIR}/qml")
target_link_libraries(test_holonight_packages PRIVATE
  holonight_package_icons
  GTest::gtest GTest::gmock Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test Qt6::Concurrent Qt6::DBus
  holonight_packaged_service
  holonight_packages_domain
  holonight_packages_backends
  holonight_packages_persistence
  holonight_packages_snapshot_store
  holonight_packages_application
)
gtest_discover_tests(test_holonight_packages
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LD_LIBRARY_PATH=$<TARGET_FILE_DIR:HoloNight::Config>")

if(ENABLE_COVERAGE)
  target_compile_options(test_holonight_packages PRIVATE --coverage)
  target_link_options(test_holonight_packages PRIVATE --coverage)
  add_custom_target(coverage
    COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/coverage
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
    COMMAND $<TARGET_FILE:test_holonight_packages>
    COMMAND ${GCOVR} --root ${CMAKE_SOURCE_DIR} --filter "${CMAKE_SOURCE_DIR}/src/"
      --html-details ${CMAKE_BINARY_DIR}/coverage/index.html --exclude-unreachable-branches --print-summary
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS test_holonight_packages holonight-packages
    VERBATIM)
endif()

add_test(NAME packaged_install_layout
  COMMAND ${CMAKE_COMMAND} "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DSTAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-stage"
    "-DINSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}" -P "${CMAKE_CURRENT_SOURCE_DIR}/packaged/check_install.cmake")
add_test(NAME helper_artifacts
  COMMAND python3 "${PROJECT_SOURCE_DIR}/scripts/check-helper-artifacts.py" --root "${PROJECT_SOURCE_DIR}")
add_test(NAME helper_artifacts_fixtures
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/policy/test_helper_artifacts.py")
add_test(NAME helper_install_layout
  COMMAND ${CMAKE_COMMAND} "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DSTAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/helper-install-stage"
    "-DINSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}" -P "${CMAKE_CURRENT_SOURCE_DIR}/helper/check_install.cmake")
# REQ-C-003: the helper and the transaction library start no external program; libalpm itself runs hooks and scriptlets.
add_test(NAME no_external_process
  COMMAND sh -c "! grep -rnE '\\b(system|popen)[[:space:]]*\\(|posix_spawn|QProcess|\\bexec(l|le|lp|v|ve|vp|vpe)[[:space:]]*\\(|\\bfork[[:space:]]*\\(' \"$1/apps/helper\" \"$1/src/transaction\"" sh "${PROJECT_SOURCE_DIR}")
# REQ-C-004: one version comparison, libalpm's, in one file of the transaction library.
add_test(NAME version_policy
  COMMAND sh -c "! grep -rlE 'alpm_pkg_vercmp|compareVersions|versionCompare' \"$1/apps/helper\" \"$1/src/domain\" \"$1/src/application\" \"$1/src/transaction\" | grep -v 'version_policy'" sh "${PROJECT_SOURCE_DIR}")
add_test(NAME runtime_qml_import_policy
  COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/check-runtime-qml-imports.sh" "${PROJECT_SOURCE_DIR}")
add_test(NAME layering_policy
  COMMAND python3 "${PROJECT_SOURCE_DIR}/scripts/check-layering.py" --root "${PROJECT_SOURCE_DIR}")
add_test(NAME layering_policy_fixtures
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/policy/test_layering_policy.py")
add_test(NAME runtime_qml_import_policy_fixtures
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_import_policy.py")

find_package(Qt6 ${Qt6_VERSION} EXACT REQUIRED COMPONENTS QmlPrivate)
qt_add_executable(test_runtime_controls
  runtime/main.cpp runtime/test_runtime_controls.cpp
  runtime/test_transaction_dialogs.cpp
  apps/installed_packages_view_test.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/TransactionController.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/TransactionController.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/TransactionRowModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/TransactionRowModel.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/TransactionMessages.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesModel.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesFilterModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesFilterModel.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdatesModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdatesModel.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/ExploreModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/ExploreModel.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/DataFreshness.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/DataFreshness.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/SnapshotFileReader.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/SnapshotFileReader.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdateCheckModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdateCheckModel.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdateCheckClient.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/UpdateCheckClient.h)
set_target_properties(test_runtime_controls PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime")
foreach(qml_file IN LISTS HOLONIGHT_PACKAGES_QML_FILES)
  cmake_path(RELATIVE_PATH qml_file BASE_DIRECTORY "${PROJECT_SOURCE_DIR}/qml" OUTPUT_VARIABLE qml_alias)
  set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()
set_source_files_properties("${PROJECT_SOURCE_DIR}/assets/holonight-pkg-manager.svg"
  PROPERTIES QT_RESOURCE_ALIAS "assets/holonight-pkg-manager.svg")
qt_add_qml_module(test_runtime_controls
  URI HolonightPackages VERSION 1.0 RESOURCE_PREFIX "/" NO_IMPORT_SCAN
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime/HolonightPackages"
  QML_FILES ${HOLONIGHT_PACKAGES_QML_FILES}
  RESOURCES ${HOLONIGHT_PACKAGES_RESOURCES})
qt_add_resources(test_runtime_controls runtime_style_config
  PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/apps/packages"
  FILES "${PROJECT_SOURCE_DIR}/apps/packages/qtquickcontrols2.conf")
target_include_directories(test_runtime_controls PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/application" "${CMAKE_CURRENT_SOURCE_DIR}/apps"
  "${PROJECT_SOURCE_DIR}/apps/packages/app")
target_link_libraries(test_runtime_controls PRIVATE
  holonight_package_icons
  GTest::gtest GTest::gmock Qt6::Quick Qt6::Qml Qt6::QmlPrivate Qt6::Test Qt6::Concurrent
  holonight_packages_application holonight_packages_snapshot_store holonight_packages_domain)
target_compile_definitions(test_runtime_controls PRIVATE
  HOLONIGHT_COMPILED_QML_ACCEPTANCE
  HOLONIGHT_QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH}")
foreach(style IN ITEMS Holonight Fusion)
  add_test(NAME runtime_controls_${style} COMMAND test_runtime_controls)
  set_tests_properties(runtime_controls_${style} PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=${style};LD_LIBRARY_PATH=$<TARGET_FILE_DIR:HoloNight::Config>")
endforeach()

# Fixture worlds for the privileged transaction core: temp-dir roots, file:// repositories, signed packages.
find_package(PkgConfig REQUIRED)
pkg_check_modules(Archive REQUIRED IMPORTED_TARGET libarchive)
pkg_check_modules(Alpm REQUIRED IMPORTED_TARGET libalpm)
add_library(holonight_transaction_test_support STATIC
  support/fixture_root.cpp
  support/gpg_keyring.cpp
  support/package_spec.cpp
  support/repo_builder.cpp
)
target_include_directories(holonight_transaction_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_compile_features(holonight_transaction_test_support PUBLIC cxx_std_23)
target_link_libraries(holonight_transaction_test_support PUBLIC
  holonight_packages_transaction Qt6::Core PkgConfig::Archive)

# The privileged transaction core runs against temporary fixture roots only (REQ-C-009, REQ-C-010).
add_executable(test_holonight_transaction
  transaction/main.cpp
  transaction/database_lock_test.cpp
  transaction/download_classifier_test.cpp
  transaction/error_mapping_test.cpp
  transaction/event_mapper_test.cpp
  transaction/execution_test.cpp
  transaction/fixture_root_test.cpp
  transaction/gpg_keyring_test.cpp
  transaction/hook_preview_test.cpp
  transaction/plan_resolver_test.cpp
  transaction/question_policy_test.cpp
  transaction/version_policy_test.cpp
)
target_include_directories(test_holonight_transaction PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/support
  ${PROJECT_SOURCE_DIR}/src/transaction/src)
target_compile_features(test_holonight_transaction PRIVATE cxx_std_23)
target_link_libraries(test_holonight_transaction PRIVATE
  GTest::gtest GTest::gmock holonight_transaction_test_support PkgConfig::Alpm holonight_packages_pacman_config)
add_executable(holonight_lock_finalization_child transaction/lock_finalization_child.cpp)
target_include_directories(holonight_lock_finalization_child PRIVATE
  ${PROJECT_SOURCE_DIR}/src/transaction/src)
target_link_libraries(holonight_lock_finalization_child PRIVATE holonight_transaction_test_support holonight_packages_pacman_config PkgConfig::Alpm)
target_compile_features(holonight_lock_finalization_child PRIVATE cxx_std_23)
add_dependencies(test_holonight_transaction holonight_lock_finalization_child)
target_compile_definitions(test_holonight_transaction PRIVATE
  HOLONIGHT_LOCK_CHILD="$<TARGET_FILE:holonight_lock_finalization_child>")
gtest_discover_tests(test_holonight_transaction
  TEST_PREFIX "holonight_transaction."
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 10)

# The helper's service layer on a private test bus, with fakes for the engine and the authorizer.
add_executable(test_holonight_package_helper
  helper/main.cpp
  helper/helper_config_test.cpp
  helper/helper_dbus_test.cpp
  helper/helper_process_test.cpp
  helper/journal_terminator_test.cpp
  helper/wire_types_test.cpp
)
set_target_properties(test_holonight_package_helper PROPERTIES AUTOMOC ON)
target_include_directories(test_holonight_package_helper PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/support
  ${CMAKE_CURRENT_SOURCE_DIR}/packaged)
target_compile_features(test_holonight_package_helper PRIVATE cxx_std_23)
target_link_libraries(test_holonight_package_helper PRIVATE
  GTest::gtest GTest::gmock Qt6::Core Qt6::DBus Qt6::Test holonight_package_helper_service
  holonight_transaction_test_support)
add_dependencies(test_holonight_package_helper holonight-package-helper)
target_compile_definitions(test_holonight_package_helper PRIVATE
  HOLONIGHT_PACKAGE_HELPER_EXECUTABLE="$<TARGET_FILE:holonight-package-helper>")
gtest_discover_tests(test_holonight_package_helper
  TEST_PREFIX "holonight_helper."
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 30)

# Transaction view-model and dialogs: Qt Quick, but no QtDBus (the controller talks to the port only).
add_executable(test_holonight_packages_transactions
  transactions/main.cpp
  transactions/transaction_row_model_test.cpp
  transactions/transaction_controller_test.cpp
  transactions/window_close_guard_test.cpp
  transactions/transaction_dialog_view_test.cpp
  transactions/transaction_buttons_test.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/TransactionRowModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/TransactionController.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/WindowCloseGuard.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/TransactionMessages.cpp
)
set_target_properties(test_holonight_packages_transactions PROPERTIES AUTOMOC ON)
target_include_directories(test_holonight_packages_transactions PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/application
  ${CMAKE_CURRENT_SOURCE_DIR}/apps
  ${CMAKE_CURRENT_SOURCE_DIR}/support
  ${PROJECT_SOURCE_DIR}/apps/packages/app)
target_compile_features(test_holonight_packages_transactions PRIVATE cxx_std_23)
target_compile_definitions(test_holonight_packages_transactions PRIVATE
  HOLONIGHT_QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH}"
  HOLONIGHT_QML_SOURCE_DIR="${PROJECT_SOURCE_DIR}/qml")
target_link_libraries(test_holonight_packages_transactions PRIVATE
  GTest::gtest GTest::gmock Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test Qt6::Concurrent
  holonight_packages_application holonight_packages_domain)
gtest_discover_tests(test_holonight_packages_transactions
  TEST_PREFIX "holonight_transactions."
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LD_LIBRARY_PATH=$<TARGET_FILE_DIR:HoloNight::Config>")
# Qt Gui itself pulls in libQt6DBus for its platform integration, so ldd would always show it. What matters is that
# this binary does not depend on QtDBus directly (its own NEEDED entries).
add_test(NAME holonight_packages_transactions_no_qtdbus
  COMMAND sh -c "! readelf -d \"$1\" | grep -q 'NEEDED.*libQt6DBus'" sh $<TARGET_FILE:test_holonight_packages_transactions>)

# The GUI's helper client against a scripted fake helper on a private dbus-daemon. Every test here needs the daemon,
# so with it missing no test in this binary passes (REQ-NF-005, REQ-NF-006).
add_executable(test_holonight_packages_helper_client
  helper_client/main.cpp
  helper_client/fake_helper_object_test.cpp
  helper_client/dbus_transaction_service_test.cpp
  helper_client/helper_contract_test.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/HelperWire.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/DBusTransactionService.cpp
)
set_target_properties(test_holonight_packages_helper_client PROPERTIES AUTOMOC ON)
target_include_directories(test_holonight_packages_helper_client PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/support
  ${CMAKE_CURRENT_SOURCE_DIR}/packaged
  ${CMAKE_CURRENT_SOURCE_DIR}/helper_client
  ${PROJECT_SOURCE_DIR}/apps/packages/app)
target_compile_features(test_holonight_packages_helper_client PRIVATE cxx_std_23)
target_compile_definitions(test_holonight_packages_helper_client PRIVATE
  HOLONIGHT_HELPER_DBUS_XML="${PROJECT_SOURCE_DIR}/apps/helper/dbus/org.holonight.PackageHelper1.xml")
target_link_libraries(test_holonight_packages_helper_client PRIVATE
  GTest::gtest GTest::gmock Qt6::Core Qt6::DBus Qt6::Test holonight_packages_domain holonight_package_helper_service
  holonight_transaction_test_support)
gtest_discover_tests(test_holonight_packages_helper_client
  TEST_PREFIX "holonight_helper_client."
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 60)
# With dbus-daemon off PATH the binary must fail, not pass or skip (REQ-NF-006).
add_test(NAME helper_client_requires_dbus_daemon
  COMMAND ${CMAKE_COMMAND} -E env PATH=/nonexistent $<TARGET_FILE:test_holonight_packages_helper_client>)
set_tests_properties(helper_client_requires_dbus_daemon PROPERTIES
  WILL_FAIL TRUE TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The polkit client against a fake polkitd. It is the only test binary that links the polkit code (REQ-C-007).
add_executable(test_holonight_package_helper_polkit
  helper/polkit_main.cpp
  helper/polkit_authorizer_test.cpp
)
set_target_properties(test_holonight_package_helper_polkit PROPERTIES AUTOMOC ON)
target_include_directories(test_holonight_package_helper_polkit PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/support
  ${CMAKE_CURRENT_SOURCE_DIR}/packaged)
target_compile_features(test_holonight_package_helper_polkit PRIVATE cxx_std_23)
target_link_libraries(test_holonight_package_helper_polkit PRIVATE
  GTest::gtest GTest::gmock Qt6::Core Qt6::DBus holonight_package_helper_polkit)
gtest_discover_tests(test_holonight_package_helper_polkit
  TEST_PREFIX "holonight_helper_polkit."
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 30)

# Hook and scriptlet tests need chroot, so the whole binary runs inside a user namespace (REQ-C-014). A statically linked
# marker tool is the hook command, since the fixture root has no libc.
add_executable(holonight_test_marker_tool support/tools/marker_tool.cpp)
target_link_options(holonight_test_marker_tool PRIVATE -static)
add_executable(test_holonight_transaction_userns
  transaction/userns_main.cpp
  transaction/userns_hook_test.cpp
)
add_dependencies(test_holonight_transaction_userns holonight_test_marker_tool)
target_include_directories(test_holonight_transaction_userns PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_compile_features(test_holonight_transaction_userns PRIVATE cxx_std_23)
target_compile_definitions(test_holonight_transaction_userns PRIVATE
  HOLONIGHT_TEST_MARKER_TOOL="$<TARGET_FILE:holonight_test_marker_tool>")
target_link_libraries(test_holonight_transaction_userns PRIVATE
  GTest::gtest GTest::gmock holonight_transaction_test_support PkgConfig::Alpm holonight_packages_pacman_config)
# Exit 77 is a skip (namespaces unavailable); HOLONIGHT_REQUIRE_USERNS=1 turns that into a failure for CI.
gtest_discover_tests(test_holonight_transaction_userns
  TEST_PREFIX "holonight_userns."
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS userns)
add_test(NAME userns_skip_policy
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/transaction/userns_policy.sh" skip $<TARGET_FILE:test_holonight_transaction_userns>)
add_test(NAME userns_require_policy
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/transaction/userns_policy.sh" require $<TARGET_FILE:test_holonight_transaction_userns>)
