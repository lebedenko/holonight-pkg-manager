add_library(holonight_packages_domain STATIC
  src/explore_source.cpp
  src/package_source.cpp
  src/update_checker.cpp
  src/update_snapshot_store.cpp
  src/transaction_error.cpp
  src/transaction_plan.cpp
  src/transaction_progress.cpp
  src/transaction_request.cpp
  src/transaction_result.cpp
  src/transaction_service.cpp
  src/update_source.cpp
)
target_include_directories(holonight_packages_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(holonight_packages_domain PRIVATE Qt6::Core)
target_compile_features(holonight_packages_domain PUBLIC cxx_std_23)
