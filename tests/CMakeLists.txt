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

set(GTEST_LIBRARY_PATH ${GTEST_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gtest.a)
set(GTEST_LIBRARY gtest)
add_library(${GTEST_LIBRARY} STATIC IMPORTED)
set_target_properties(${GTEST_LIBRARY} PROPERTIES
        IMPORTED_LOCATION ${GTEST_LIBRARY_PATH}
        INTERFACE_LINK_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
add_dependencies(${GTEST_LIBRARY} gtest-proj)

set(GTEST_MAIN_LIBRARY_PATH ${GTEST_ROOT}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gtest_main.a)
set(GTEST_MAIN_LIBRARY gtest-main)
add_library(${GTEST_MAIN_LIBRARY} STATIC IMPORTED)
set_target_properties(${GTEST_MAIN_LIBRARY} PROPERTIES
        IMPORTED_LOCATION ${GTEST_MAIN_LIBRARY_PATH}
        INTERFACE_LINK_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
add_dependencies(${GTEST_MAIN_LIBRARY} gtest-proj)

add_executable(value value.cpp)
target_include_directories(value PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(value mgclient-static gtest gtest-main)
add_test(value value)

add_executable(encoder encoder.cpp)
target_include_directories(encoder PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(encoder mgclient-static gtest gtest-main)
add_test(encoder encoder)

add_executable(decoder decoder.cpp)
target_include_directories(decoder PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(decoder mgclient-static gtest gtest-main)
add_test(decoder decoder)

add_executable(client client.cpp)
target_include_directories(client PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(client mgclient-static gtest gtest-main)
# We're mocking the mg_secure_transport_init function in the test.
target_link_libraries(client -Wl,--wrap=mg_secure_transport_init)
add_test(client client)

add_executable(transport transport.cpp)
target_include_directories(transport PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(transport mgclient-static gtest gtest-main stdc++fs)
add_test(transport transport)

add_executable(allocator allocator.cpp)
target_include_directories(allocator PRIVATE ${GTEST_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(allocator mgclient-static gtest gtest-main)
add_test(allocator allocator)
