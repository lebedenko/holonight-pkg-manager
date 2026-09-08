include(GoogleTest)

add_executable(test_holonight_packages
  main.cpp
  test_placeholder.cpp
  domain/package_test.cpp
  backends/alpm_package_source_test.cpp
  persistence/alpm_connection_cache_test.cpp
  application/package_list_use_case_test.cpp
  application/orphan_package_filter_test.cpp
  application/package_size_formatter_test.cpp
  apps/installed_packages_model_test.cpp
  apps/installed_packages_filter_model_test.cpp
  apps/installed_packages_view_test.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesFilterModel.cpp
)
qt_add_resources(test_holonight_packages workspace_assets
  PREFIX "/HolonightPackages"
  BASE "${PROJECT_SOURCE_DIR}"
  FILES "${PROJECT_SOURCE_DIR}/assets/holonight-pkg-manager.svg")

target_include_directories(test_holonight_packages PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/application
  ${PROJECT_SOURCE_DIR}/apps/packages/app
  ${PROJECT_SOURCE_DIR}/src/backends/src)
set_target_properties(test_holonight_packages PROPERTIES AUTOMOC ON)
target_compile_features(test_holonight_packages PRIVATE cxx_std_23)
target_compile_definitions(test_holonight_packages PRIVATE
  HOLONIGHT_QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH}"
  HOLONIGHT_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  HOLONIGHT_QML_SOURCE_DIR="${PROJECT_SOURCE_DIR}/qml")
target_link_libraries(test_holonight_packages PRIVATE
  GTest::gtest GTest::gmock Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test Qt6::Concurrent
  holonight_packages_domain
  holonight_packages_backends
  holonight_packages_persistence
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

add_test(NAME runtime_qml_import_policy
  COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/check-runtime-qml-imports.sh" "${PROJECT_SOURCE_DIR}")
add_test(NAME runtime_qml_import_policy_fixtures
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_import_policy.py")

find_package(Qt6 REQUIRED COMPONENTS QmlPrivate)
qt_add_executable(test_runtime_controls
  runtime/main.cpp runtime/test_runtime_controls.cpp
  apps/installed_packages_view_test.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesModel.h
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesFilterModel.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesFilterModel.h)
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
  "${CMAKE_CURRENT_SOURCE_DIR}/application" "${PROJECT_SOURCE_DIR}/apps/packages/app")
target_link_libraries(test_runtime_controls PRIVATE
  GTest::gtest GTest::gmock Qt6::Quick Qt6::Qml Qt6::QmlPrivate Qt6::Test Qt6::Concurrent
  holonight_packages_application holonight_packages_domain)
target_compile_definitions(test_runtime_controls PRIVATE
  HOLONIGHT_COMPILED_QML_ACCEPTANCE
  HOLONIGHT_QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH}")
foreach(style IN ITEMS Holonight Fusion)
  add_test(NAME runtime_controls_${style} COMMAND test_runtime_controls)
  set_tests_properties(runtime_controls_${style} PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=${style};LD_LIBRARY_PATH=$<TARGET_FILE_DIR:HoloNight::Config>")
endforeach()
