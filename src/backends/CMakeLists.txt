find_package(PkgConfig REQUIRED)
pkg_check_modules(Alpm REQUIRED IMPORTED_TARGET libalpm)

add_library(holonight_packages_backends STATIC
  src/alpm_package_source.cpp
)
target_include_directories(holonight_packages_backends PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(holonight_packages_backends
  PUBLIC holonight_packages_domain Qt6::Core Qt6::Network
  PRIVATE PkgConfig::Alpm
)
target_compile_features(holonight_packages_backends PUBLIC cxx_std_23)
