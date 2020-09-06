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

# Setup GTest
find_package(Threads REQUIRED)

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

set(GTEST_LIBRARY_PATH ${GTEST_ROOT}/lib/${MGCLIENT_FIND_LIBRARY_PREFIXES}gtest.a)
set(GTEST_DEBUG_LIBRARY_PATH ${GTEST_ROOT}/lib/${MGCLIENT_FIND_LIBRARY_PREFIXES}gtestd.a)
set(GTEST_LIBRARY gtest)
add_library(${GTEST_LIBRARY} STATIC IMPORTED)
# TODO(gitbuda): On Windows ${CMAKE_THREAD_LIBS_INIT} is undefined.
#     "${...}" just prevents set_target_properties to fail.
#     It's not a solution.
set_target_properties(${GTEST_LIBRARY} PROPERTIES
        IMPORTED_LOCATION ${GTEST_LIBRARY_PATH}
        IMPORTED_LOCATION_DEBUG ${GTEST_DEBUG_LIBRARY_PATH}
        INTERFACE_LINK_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}")
add_dependencies(${GTEST_LIBRARY} gtest-proj)

set(GTEST_MAIN_LIBRARY_PATH ${GTEST_ROOT}/lib/${MGCLIENT_FIND_LIBRARY_PREFIXES}gtest_main.a)
set(GTEST_MAIN_DEBUG_LIBRARY_PATH ${GTEST_ROOT}/lib/${MGCLIENT_FIND_LIBRARY_PREFIXES}gtest_maind.a)
set(GTEST_MAIN_LIBRARY gtest-main)
add_library(${GTEST_MAIN_LIBRARY} STATIC IMPORTED)
set_target_properties(${GTEST_MAIN_LIBRARY} PROPERTIES
        IMPORTED_LOCATION ${GTEST_MAIN_LIBRARY_PATH}
        IMPORTED_LOCATION_DEBUG ${GTEST_MAIN_DEBUG_LIBRARY_PATH}
        INTERFACE_LINK_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}")
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
