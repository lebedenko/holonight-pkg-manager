add_library(holonight_packages_application STATIC
  src/explore_search.cpp
  src/package_list_use_case.cpp
  src/orphan_package_filter.cpp
  src/package_size_formatter.cpp
  include/holonight_packages_application/update_monitor.h
  src/snapshot_selection.cpp
  src/update_age_format.cpp
  src/update_check_policy.cpp
  src/update_check_ports.cpp
  src/update_check_runtime.cpp
  include/holonight_packages_application/update_check_scheduler.h
  src/update_check_scheduler.cpp
  include/holonight_packages_application/update_check_service.h
  src/update_check_service.cpp
  src/update_monitor.cpp
  src/update_status.cpp
  src/update_summary.cpp
)
target_include_directories(holonight_packages_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(ICU REQUIRED COMPONENTS uc)
target_link_libraries(holonight_packages_application
  PUBLIC holonight_packages_domain Qt6::Core Qt6::Concurrent
  PRIVATE ICU::uc)
target_compile_features(holonight_packages_application PUBLIC cxx_std_23)
