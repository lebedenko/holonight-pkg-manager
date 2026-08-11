qt_add_executable(holonight-packages
  main.cpp
  app/PackagesApplication.h
  app/PackagesApplication.cpp
  app/InstalledPackagesModel.h
  app/InstalledPackagesModel.cpp
)

set_target_properties(holonight-packages PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

file(GLOB_RECURSE HOLONIGHT_PACKAGES_QML_FILES
  LIST_DIRECTORIES false CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/qml/*.qml")
list(SORT HOLONIGHT_PACKAGES_QML_FILES)

foreach(qml_file IN LISTS HOLONIGHT_PACKAGES_QML_FILES)
  cmake_path(RELATIVE_PATH qml_file BASE_DIRECTORY "${PROJECT_SOURCE_DIR}/qml" OUTPUT_VARIABLE qml_alias)
  set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()

set(HOLONIGHT_PACKAGES_RESOURCES "${PROJECT_SOURCE_DIR}/assets/holonight-pkg-manager.svg")
set_source_files_properties("${PROJECT_SOURCE_DIR}/assets/holonight-pkg-manager.svg"
  PROPERTIES QT_RESOURCE_ALIAS "assets/holonight-pkg-manager.svg")

qt_add_qml_module(holonight-packages
  URI HolonightPackages
  VERSION 1.0
  RESOURCE_PREFIX "/"
  NO_IMPORT_SCAN
  QML_FILES ${HOLONIGHT_PACKAGES_QML_FILES}
  SOURCES app/InstalledPackagesModel.h
  RESOURCES ${HOLONIGHT_PACKAGES_RESOURCES}
)

target_link_libraries(holonight-packages PRIVATE
  HolonightQt::Controls
  holonight_packages_domain
  holonight_packages_application
  holonight_packages_backends
  holonight_packages_advisor
  holonight_packages_persistence
  holonight_packages_platform
  Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Concurrent
)

target_compile_definitions(holonight-packages PRIVATE HOLONIGHT_PACKAGES_VERSION="${PROJECT_VERSION}")
target_include_directories(holonight-packages PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_compile_features(holonight-packages PRIVATE cxx_std_23)

install(TARGETS holonight-packages DESTINATION ${CMAKE_INSTALL_BINDIR})
