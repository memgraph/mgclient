add_library(mgclient_cpp INTERFACE)
target_include_directories(mgclient_cpp INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
install(DIRECTORY
        "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
target_link_libraries(mgclient_cpp INTERFACE mgclient-static)
