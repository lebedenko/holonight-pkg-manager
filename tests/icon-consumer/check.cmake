# This component stages only a private test seam; ordinary installation excludes it.
file(REMOVE_RECURSE "${TEST_ROOT}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${PROVIDER_BUILD}" --prefix "${TEST_ROOT}/prefix" --component icon-probe COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${TEST_ROOT}/build" -G Ninja
  "-DICON_PREFIX=${TEST_ROOT}/prefix" "-DCMAKE_PREFIX_PATH=${DEPENDENCY_PREFIX}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TEST_ROOT}/build" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
  "${TEST_ROOT}/build/package-icon-probe" --without-jxl "${FIXTURE}" COMMAND_ERROR_IS_FATAL ANY)
if(REQUIRE_JXL)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
    "${TEST_ROOT}/build/package-icon-probe" --require-jxl "${FIXTURE}" COMMAND_ERROR_IS_FATAL ANY)
endif()
