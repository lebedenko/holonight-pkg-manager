include(GoogleTest)

add_executable(test_holonight_packages
  main.cpp
  test_placeholder.cpp
  domain/package_test.cpp
  backends/alpm_package_source_test.cpp
  persistence/alpm_connection_cache_test.cpp
  application/package_list_use_case_test.cpp
  apps/installed_packages_model_test.cpp
  apps/installed_packages_view_test.cpp
  ${PROJECT_SOURCE_DIR}/apps/packages/app/InstalledPackagesModel.cpp
)
target_include_directories(test_holonight_packages PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/application
  ${PROJECT_SOURCE_DIR}/apps/packages/app
  ${PROJECT_SOURCE_DIR}/src/backends/src)
set_target_properties(test_holonight_packages PROPERTIES AUTOMOC ON)
target_compile_features(test_holonight_packages PRIVATE cxx_std_23)
target_compile_definitions(test_holonight_packages PRIVATE
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
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
