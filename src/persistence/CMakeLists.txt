find_package(PkgConfig REQUIRED)
pkg_check_modules(Alpm REQUIRED IMPORTED_TARGET libalpm)

add_library(holonight_packages_persistence STATIC
  src/alpm_connection_cache.cpp
)
target_include_directories(holonight_packages_persistence PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(holonight_packages_persistence
  PUBLIC holonight_packages_domain Qt6::Core Qt6::Sql PkgConfig::Alpm
)
target_compile_features(holonight_packages_persistence PUBLIC cxx_std_23)

# Libalpm-free store for the update snapshot: application-layer tests and the GUI link this, not the target above.
add_library(holonight_packages_snapshot_store STATIC
  src/cache_locations.cpp
  src/json_update_snapshot_store.cpp
)
target_include_directories(holonight_packages_snapshot_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(holonight_packages_snapshot_store PUBLIC holonight_packages_domain Qt6::Core)
target_compile_features(holonight_packages_snapshot_store PUBLIC cxx_std_23)
