find_package(Qt6 REQUIRED COMPONENTS DBus)

# The introspection XML becomes one C string literal, so the adaptor answers Introspect() with the installed file's text.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/dbus/org.holonight.PackageHelper1.xml" HOLONIGHT_HELPER_XML)
string(REGEX MATCH "<interface.*</interface>" HOLONIGHT_HELPER_INTERFACE "${HOLONIGHT_HELPER_XML}")
string(REPLACE "\\" "\\\\" HOLONIGHT_HELPER_INTERFACE "${HOLONIGHT_HELPER_INTERFACE}")
string(REPLACE "\"" "\\\"" HOLONIGHT_HELPER_INTERFACE "${HOLONIGHT_HELPER_INTERFACE}")
string(REPLACE "\n" "\\n\" \\\n  \"" HOLONIGHT_HELPER_INTERFACE "${HOLONIGHT_HELPER_INTERFACE}")
set(HOLONIGHT_HELPER_INTERFACE_LITERAL "\"${HOLONIGHT_HELPER_INTERFACE}\"")
configure_file(HelperIntrospection.h.in "${CMAKE_CURRENT_BINARY_DIR}/HelperIntrospection.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/dbus/org.holonight.PackageHelper1.xml")

add_library(holonight_package_helper_service STATIC
  DBusCallerIdentity.h
  DBusCallerIdentity.cpp
  HelperAdaptor.h
  HelperAdaptor.cpp
  HelperConfig.h
  HelperConfig.cpp
  HelperIdentity.h
  HelperService.h
  HelperService.cpp
  StateDirScratchRoots.cpp
  StateDirScratchRoots.h
  HelperSignals.h
  HelperSignals.cpp
  HelperTerminator.h
  HelperTerminator.cpp
  JournalLogger.h
  JournalLogger.cpp
  QtWorkRunner.h
  QtWorkRunner.cpp
  WireTypes.h
  WireTypes.cpp
)
target_include_directories(holonight_package_helper_service PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}/src/backends/include)
target_link_libraries(holonight_package_helper_service PUBLIC
  holonight_packages_application holonight_packages_domain holonight_packages_transaction Qt6::Core Qt6::DBus)
target_compile_features(holonight_package_helper_service PUBLIC cxx_std_23)
target_compile_definitions(holonight_package_helper_service PUBLIC
  HOLONIGHT_HELPER_DBUS_XML="${CMAKE_CURRENT_SOURCE_DIR}/dbus/org.holonight.PackageHelper1.xml")

# The polkit client lives apart so only the production composition (and its own wire test) links it (REQ-C-007).
add_library(holonight_package_helper_polkit STATIC PolkitAuthorizer.h PolkitAuthorizer.cpp)
target_include_directories(holonight_package_helper_polkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(holonight_package_helper_polkit PUBLIC holonight_packages_application Qt6::Core Qt6::DBus)
target_compile_features(holonight_package_helper_polkit PUBLIC cxx_std_23)

add_executable(holonight-package-helper main.cpp)
set_target_properties(holonight-package-helper PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_link_libraries(holonight-package-helper PRIVATE
  holonight_package_helper_service holonight_package_helper_polkit holonight_packages_transaction
  holonight_packages_application Qt6::Core Qt6::DBus)
target_compile_definitions(holonight-package-helper PRIVATE HOLONIGHT_PACKAGE_HELPER_VERSION="${PROJECT_VERSION}")
target_compile_features(holonight-package-helper PRIVATE cxx_std_23)

# Installable artifacts. Tests generate their own activation file; the installed one starts the unit (REQ-F-067).
set(HOLONIGHT_HELPER_EXECUTABLE "${CMAKE_INSTALL_FULL_LIBEXECDIR}/holonight-package-helper")
configure_file(data/org.holonight.PackageHelper1.service.in
  "${CMAKE_CURRENT_BINARY_DIR}/org.holonight.PackageHelper1.service" @ONLY)
configure_file(data/holonight-package-helper.service.in
  "${CMAKE_CURRENT_BINARY_DIR}/holonight-package-helper.service" @ONLY)

install(TARGETS holonight-package-helper DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES data/org.holonight.packages.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES data/org.holonight.PackageHelper1.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.holonight.PackageHelper1.service"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system-services)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/holonight-package-helper.service"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
install(FILES dbus/org.holonight.PackageHelper1.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/interfaces)
