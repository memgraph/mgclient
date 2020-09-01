# Copyright (c) 2016-2020 Memgraph Ltd. [https://memgraph.com]
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

include(ExternalProject)

find_package(Threads REQUIRED)

# Setup gflags
set(GFLAGS_ROOT ${PROJECT_BINARY_DIR}/gflags)
ExternalProject_Add(gflags-proj
  PREFIX            ${GFLAGS_ROOT}
  INSTALL_DIR       ${GFLAGS_ROOT}
  GIT_REPOSITORY    https://github.com/gflags/gflags.git
  GIT_TAG           v2.2.2
  CMAKE_ARGS        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
                    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

set(GFLAGS_INCLUDE_DIR ${GFLAGS_ROOT}/include)
set(GFLAGS_LIBRARY_PATH ${GFLAGS_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gflags.a)
set(GFLAGS_DEBUG_LIBRARY_PATH ${GFLAGS_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gflags_debug.a)
add_library(gflags STATIC IMPORTED)
set_target_properties(gflags PROPERTIES
  IMPORTED_LOCATION ${GFLAGS_LIBRARY_PATH}
  IMPORTED_LOCATION_DEBUG ${GFLAGS_DEBUG_LIBRARY_PATH}
  INTERFACE_LINK_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
add_dependencies(gflags gflags-proj)


# Setup glog
set(GLOG_ROOT ${PROJECT_BINARY_DIR}/glog)
ExternalProject_Add(glog-proj
  PREFIX            ${GLOG_ROOT}
  INSTALL_DIR       ${GLOG_ROOT}
  GIT_REPOSITORY    https://github.com/google/glog.git
  GIT_TAG           v0.4.0
  CMAKE_ARGS        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
                    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
                    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
  DEPENDS           gflags-proj)

set(GLOG_INCLUDE_DIR ${GLOG_ROOT}/include)
set(GLOG_LIBRARY_PATH ${GLOG_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}glog.a)
set(GLOG_DEBUG_LIBRARY_PATH ${GLOG_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}glogd.a)
add_library(glog STATIC IMPORTED)
set_target_properties(glog PROPERTIES
  IMPORTED_LOCATION ${GLOG_LIBRARY_PATH}
  IMPORTED_LOCATION_DEBUG ${GLOG_DEBUG_LIBRARY_PATH}
  INTERFACE_LINK_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
add_dependencies(glog glog-proj)

# Setup GTest
ExternalProject_Add(gtest-proj
        PREFIX            gtest
        GIT_REPOSITORY    https://github.com/google/googletest.git
        GIT_TAG           release-1.8.1
        CMAKE_ARGS        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
                          "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        INSTALL_DIR       "${PROJECT_BINARY_DIR}/gtest")

ExternalProject_Get_Property(gtest-proj install_dir)
set(GTEST_ROOT ${install_dir})

set(GTEST_INCLUDE_DIRS ${GTEST_ROOT}/include
        CACHE INTERNAL "Path to gtest include directory")

set(GTEST_LIBRARY_PATH ${GTEST_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gtest.a)
set(GTEST_DEBUG_LIBRARY_PATH ${GTEST_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gtestd.a)
set(GTEST_LIBRARY gtest)
add_library(${GTEST_LIBRARY} STATIC IMPORTED)
set_target_properties(${GTEST_LIBRARY} PROPERTIES
        IMPORTED_LOCATION ${GTEST_LIBRARY_PATH}
        IMPORTED_LOCATION_DEBUG ${GTEST_DEBUG_LIBRARY_PATH}
        INTERFACE_LINK_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
add_dependencies(${GTEST_LIBRARY} gtest-proj)

set(GTEST_MAIN_LIBRARY_PATH ${GTEST_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gtest_main.a)
set(GTEST_MAIN_DEBUG_LIBRARY_PATH ${GTEST_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gtest_maind.a)
set(GTEST_MAIN_LIBRARY gtest-main)
add_library(${GTEST_MAIN_LIBRARY} STATIC IMPORTED)
set_target_properties(${GTEST_MAIN_LIBRARY} PROPERTIES
        IMPORTED_LOCATION ${GTEST_MAIN_LIBRARY_PATH}
        IMPORTED_LOCATION_DEBUG ${GTEST_MAIN_DEBUG_LIBRARY_PATH}
        INTERFACE_LINK_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
add_dependencies(${GTEST_MAIN_LIBRARY} gtest-proj)

macro(add_gtest target_name target_path)
  add_executable(${target_name} ${target_path})
  target_include_directories(${target_name} PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(${target_name} mgclient-static gtest gtest-main)
  add_test(${target_name} ${target_name})
endmacro()

add_gtest(value value.cpp)

add_gtest(encoder encoder.cpp)

add_gtest(decoder decoder.cpp)

add_gtest(client client.cpp)
# We're mocking the mg_secure_transport_init function in the test.
target_link_libraries(client -Wl,--wrap=mg_secure_transport_init)

add_gtest(transport transport.cpp)
target_link_libraries(transport stdc++fs)

add_gtest(allocator allocator.cpp)

if(BUILD_TESTING_INTEGRATION)
  add_gtest(basic integration/basic.cpp)
endif()

add_gtest(cpp_mgclient_value cpp/mgclient_value.cpp)
target_link_libraries(cpp_mgclient_value glog gflags)
target_include_directories(cpp_mgclient_value PRIVATE ${GLOG_INCLUDE_DIR} ${GFLAGS_INCLUDE_DIR})
