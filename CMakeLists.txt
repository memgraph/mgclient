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

cmake_minimum_required(VERSION 3.8)

project(mgclient VERSION 1.1.0)
# Minor version increase can also mean ABI incompatibility with previous
# versions. IMPORTANT: Take care of the SO version manually.
set(mgclient_SOVERSION 2)
add_definitions(-DMGCLIENT_VERSION="${mgclient_VERSION}")

if(${WIN32})
    message(STATUS "ON WINDOWS BUILD")
    add_definitions(-DON_WINDOWS=1)
    # CMAKE_FIND_LIBRARY_PREFIXES on Windows is "lib;".
    # "lib;" breaks gtest lib referencing.
    set(MGCLIENT_FIND_LIBRARY_PREFIXES "lib")
else()
    message(STATUS "ON POSIX BUILD")
    add_definitions(-DON_POSIX=1)
    set(MGCLIENT_FIND_LIBRARY_PREFIXES "${CMAKE_FIND_LIBRARY_PREFIXES}")
endif()

# Building tests is disabled by default to simplify the default build config.
option(BUILD_TESTING "" OFF)
message(STATUS "BUILD_TESTING: ${BUILD_TESTING}")
# Integration tests are disabled by default, since the memgraph instance is
# neccessary to execute them.
option(BUILD_TESTING_INTEGRATION "" OFF)
message(STATUS "BUILD_TESTING_INTEGRATION: ${BUILD_TESTING_INTEGRATION}")

include(CTest)

set(CMAKE_C_STANDARD 11)
# C++17 is fine here because it is required only for the testing purposes.
set(CMAKE_CXX_STANDARD 17)
set(C_STANDARD_REQUIRED ON)
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set default build type to 'Release'
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()
message(STATUS "CMake build type: ${CMAKE_BUILD_TYPE}")

# Set default instalation directory to '/usr'
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # '/usr' is a special case, for more details see:
    # https://cmake.org/cmake/help/latest/module/GNUInstallDirs.html#special-cases
    set(CMAKE_INSTALL_PREFIX "/usr")
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
