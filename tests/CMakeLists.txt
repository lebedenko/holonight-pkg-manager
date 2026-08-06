include(GoogleTest)

add_executable(test_holonight_packages main.cpp test_placeholder.cpp)
set_target_properties(test_holonight_packages PROPERTIES AUTOMOC ON)
target_compile_features(test_holonight_packages PRIVATE cxx_std_23)
target_link_libraries(test_holonight_packages PRIVATE GTest::gtest GTest::gmock Qt6::Gui)
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
