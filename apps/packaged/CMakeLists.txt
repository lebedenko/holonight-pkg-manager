find_package(Qt6 REQUIRED COMPONENTS DBus)

add_library(holonight_packaged_service STATIC
  PackagedConfig.h
  PackagedConfig.cpp
  UpdateCheckAdaptor.h
  UpdateCheckAdaptor.cpp
  UpdateStatusService.h
  UpdateStatusService.cpp
  UpdatesAdaptor.h
  UpdatesAdaptor.cpp
)
target_include_directories(holonight_packaged_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(holonight_packaged_service PUBLIC
  holonight_packages_application HoloNight::Config Qt6::Core Qt6::DBus)
target_compile_features(holonight_packaged_service PUBLIC cxx_std_23)
target_compile_definitions(holonight_packaged_service PUBLIC
  HOLONIGHT_PACKAGED_DBUS_XML="${CMAKE_CURRENT_SOURCE_DIR}/dbus/org.holonight.Packages1.Updates.xml")

add_executable(holonight-packaged
  main.cpp
  SignalHandler.h
  SignalHandler.cpp
)
set_target_properties(holonight-packaged PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_link_libraries(holonight-packaged PRIVATE
  holonight_packaged_service
  holonight_packages_backends
  holonight_packages_snapshot_store
  HoloNight::Config
  Qt6::Core Qt6::DBus)
target_compile_definitions(holonight-packaged PRIVATE HOLONIGHT_PACKAGED_VERSION="${PROJECT_VERSION}")
target_compile_features(holonight-packaged PRIVATE cxx_std_23)

set(HOLONIGHT_PACKAGED_EXECUTABLE "${CMAKE_INSTALL_FULL_BINDIR}/holonight-packaged")
configure_file(data/org.holonight.Packages1.service.in
  "${CMAKE_CURRENT_BINARY_DIR}/org.holonight.Packages1.service" @ONLY)
configure_file(data/holonight-packaged.service.in
  "${CMAKE_CURRENT_BINARY_DIR}/holonight-packaged.service" @ONLY)

install(TARGETS holonight-packaged DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.holonight.Packages1.service"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/holonight-packaged.service"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user)
install(FILES dbus/org.holonight.Packages1.Updates.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/interfaces)
