cmake_minimum_required(VERSION 3.25)
project(holonight-pkg-manager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
install(FILES LICENSE LICENSES/GPL-3.0-or-later.txt
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network Sql DBus Concurrent)
find_package(HolonightQt REQUIRED COMPONENTS Controls)
unset(HOLONIGHT_QML_IMPORT_PATH CACHE)
find_path(HOLONIGHT_QML_IMPORT_PATH
  NAMES Holonight/qmldir
  HINTS "${HolonightQt_DIR}/../../.."
  PATH_SUFFIXES lib/qt6/qml lib64/qt6/qml
  NO_DEFAULT_PATH REQUIRED)

qt_policy(SET QTP0004 NEW)

add_subdirectory(src/domain)
add_subdirectory(src/application)
add_subdirectory(src/persistence)
add_subdirectory(src/backends)
add_subdirectory(src/advisor)
add_subdirectory(src/platform)
add_subdirectory(apps/packages)

option(BUILD_TESTS "Build unit tests" OFF)
option(ENABLE_COVERAGE "Instrument for gcov/gcovr coverage reporting" OFF)

if(BUILD_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND OR NOT TARGET GTest::gtest OR NOT TARGET GTest::gmock)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
      URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()
  if(ENABLE_COVERAGE)
    find_program(GCOVR gcovr REQUIRED)
    target_compile_options(holonight-packages PRIVATE --coverage)
    target_link_options(holonight-packages PRIVATE --coverage)
  endif()
  add_subdirectory(tests)
endif()

find_program(CLANG_FORMAT clang-format)
find_program(CLANG_TIDY clang-tidy)
find_program(RUN_CLANG_TIDY run-clang-tidy)
find_program(QMLLINT qmllint HINTS "${Qt6_DIR}/../../../lib/qt6/bin" "/usr/lib/qt6/bin")

file(GLOB_RECURSE ALL_SOURCE_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS
  apps/*.cpp apps/*.h src/*.cpp src/*.h tests/*.cpp tests/*.h)
file(GLOB_RECURSE ALL_CXX_SOURCE_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS apps/*.cpp src/*.cpp)
file(GLOB_RECURSE ALL_CXX_TEST_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS tests/*.cpp)
list(REMOVE_ITEM ALL_CXX_TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/main.cpp")
set(ALL_CXX_FILES ${ALL_CXX_SOURCE_FILES} ${ALL_CXX_TEST_FILES})
file(GLOB_RECURSE ALL_QML_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS qml/*.qml)

if(CLANG_FORMAT)
  add_custom_target(format COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES} VERBATIM)
  add_custom_target(format-check COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCE_FILES} VERBATIM)
endif()

if(CLANG_TIDY)
  set(TIDY_JOBS "4" CACHE STRING "Maximum parallel clang-tidy processes")

  set(TIDY_DB_DIR ${CMAKE_BINARY_DIR}/tidy)
  set(STRIP_TIDY_FLAGS_SCRIPT ${CMAKE_BINARY_DIR}/strip_tidy_flags.cmake)
  file(MAKE_DIRECTORY ${TIDY_DB_DIR})
  file(WRITE ${STRIP_TIDY_FLAGS_SCRIPT}
    "file(READ \"${CMAKE_BINARY_DIR}/compile_commands.json\" db)\n"
    "string(REPLACE \"-mno-direct-extern-access\" \"\" db \"\${db}\")\n"
    "file(WRITE \"${TIDY_DB_DIR}/compile_commands.json\" \"\${db}\")\n")

  if(RUN_CLANG_TIDY)
    add_custom_target(tidy
      COMMAND ${CMAKE_COMMAND} -P ${STRIP_TIDY_FLAGS_SCRIPT}
      COMMAND ${RUN_CLANG_TIDY} -quiet -j ${TIDY_JOBS} -p ${TIDY_DB_DIR} ${ALL_CXX_FILES}
      VERBATIM)
  else()
    add_custom_target(tidy
      COMMAND ${CMAKE_COMMAND} -P ${STRIP_TIDY_FLAGS_SCRIPT}
      COMMAND ${CLANG_TIDY} --quiet -p ${TIDY_DB_DIR} ${ALL_CXX_FILES}
      VERBATIM)
  endif()
endif()

if(QMLLINT)
  add_custom_target(qml-lint
    COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:HoloNight::Config>"
      ${QMLLINT} --unused-imports disable
      -I "${CMAKE_CURRENT_BINARY_DIR}/apps/packages"
      -I "${CMAKE_BINARY_DIR}"
      -I "${HOLONIGHT_QML_IMPORT_PATH}"
      ${ALL_QML_FILES}
    DEPENDS holonight-packages_qmltyperegistration
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
else()
  add_custom_target(qml-lint
    COMMAND ${CMAKE_COMMAND} -E echo "qmllint was not found. Install qt6-declarative-dev-tools."
    COMMAND ${CMAKE_COMMAND} -E false)
endif()
