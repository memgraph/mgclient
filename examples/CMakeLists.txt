cmake_minimum_required(VERSION 3.8)

project(example VERSION 0.1)

include(ExternalProject)

set(MGCLIENT_GIT_TAG      "v1.4.0" CACHE STRING "mgclient git tag")
set(MGCLIENT_LIBRARY      mgclient-lib)
set(MGCLIENT_INSTALL_DIR  ${CMAKE_BINARY_DIR}/mgclient)
set(MGCLIENT_INCLUDE_DIRS ${MGCLIENT_INSTALL_DIR}/include)
if (UNIX AND NOT APPLE)
  set(MGCLIENT_LIBRARY_PATH ${MGCLIENT_INSTALL_DIR}/lib/libmgclient.so)
elseif (WIN32)
  set(MGCLIENT_LIBRARY_PATH ${MGCLIENT_INSTALL_DIR}/lib/mgclient.dll)
endif()
ExternalProject_Add(mgclient-proj
  PREFIX           mgclient-proj
  GIT_REPOSITORY   https://github.com/memgraph/mgclient.git
  GIT_TAG          "${MGCLIENT_GIT_TAG}"
  CMAKE_ARGS       "-DCMAKE_INSTALL_PREFIX=${MGCLIENT_INSTALL_DIR}"
                   "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
                   "-DBUILD_CPP_BINDINGS=ON"
                   "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
  BUILD_BYPRODUCTS "${MGCLIENT_LIBRARY_PATH}"
  INSTALL_DIR      "${PROJECT_BINARY_DIR}/mgclient"
)
add_library(${MGCLIENT_LIBRARY} SHARED IMPORTED)
target_compile_definitions(${MGCLIENT_LIBRARY} INTERFACE mgclient_shared_EXPORTS)
set_property(TARGET ${MGCLIENT_LIBRARY} PROPERTY IMPORTED_LOCATION ${MGCLIENT_LIBRARY_PATH})
if (WIN32)
  set_property(TARGET ${MGCLIENT_LIBRARY} PROPERTY IMPORTED_IMPLIB ${MGCLIENT_INSTALL_DIR}/lib/mgclient.lib)
endif()
add_dependencies(${MGCLIENT_LIBRARY} mgclient-proj)
