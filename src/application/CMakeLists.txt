add_library(holonight_packages_application STATIC
  src/package_list_use_case.cpp
  src/orphan_package_filter.cpp
  src/package_size_formatter.cpp
  src/update_summary.cpp
)
target_include_directories(holonight_packages_application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(holonight_packages_application PUBLIC holonight_packages_domain)
target_compile_features(holonight_packages_application PUBLIC cxx_std_23)
