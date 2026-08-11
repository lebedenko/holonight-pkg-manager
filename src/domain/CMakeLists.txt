add_library(holonight_packages_domain STATIC
  src/package_source.cpp
)
target_include_directories(holonight_packages_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(holonight_packages_domain PRIVATE Qt6::Core)
target_compile_features(holonight_packages_domain PUBLIC cxx_std_23)
