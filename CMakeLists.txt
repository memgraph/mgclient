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

if(WASM)
    execute_process(COMMAND ${CMAKE_SOURCE_DIR}/wasm/install_deps.sh)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/wasm/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
endif()

project(mgclient VERSION 1.4.0)
# Minor version increase can also mean ABI incompatibility with previous
# versions. IMPORTANT: Take care of the SO version manually.
set(mgclient_SOVERSION 2)
set(WASM OFF CACHE BOOL "Compile mgclient for wasm")
add_definitions(-DMGCLIENT_VERSION="${mgclient_VERSION}")

# Deal with the operating system.
if((NOT UNIX) AND EMSCRIPTEN)
    message(FATAL_ERROR "WASM build is only supported in Linux")
endif()

if(WIN32)
    message(STATUS "ON WINDOWS BUILD")
    set(MGCLIENT_ON_WINDOWS TRUE)
    add_definitions(-DMGCLIENT_ON_WINDOWS)
    # CMAKE_FIND_LIBRARY_PREFIXES on Windows is "lib;".
    # "lib;" breaks gtest lib referencing.
    set(MGCLIENT_FIND_LIBRARY_PREFIXES "lib")
elseif(UNIX AND NOT APPLE)
    if(EMSCRIPTEN)
        message(STATUS "ON LINUX WASM BUILD")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(STATUS "ON LINUX BUILD")
    else()
        message(FATAL_ERROR "Unsupported operating system. Please create issue or contribute!")
    endif()
    set(MGCLIENT_ON_LINUX TRUE)
    add_definitions(-DMGCLIENT_ON_LINUX)
    set(MGCLIENT_FIND_LIBRARY_PREFIXES "${CMAKE_FIND_LIBRARY_PREFIXES}")
elseif(APPLE)
    message(STATUS "ON APPLE BUILD")
    set(MGCLIENT_ON_APPLE TRUE)
    add_definitions(-DMGCLIENT_ON_APPLE)
    set(MGCLIENT_FIND_LIBRARY_PREFIXES "${CMAKE_FIND_LIBRARY_PREFIXES}")
else()
    message(FATAL_ERROR "Unsupported operating system. Please create issue or contribute!")
endif()

# Building tests is disabled by default to simplify the default build config.
option(BUILD_TESTING "" OFF)
message(STATUS "BUILD_TESTING: ${BUILD_TESTING}")
# Integration tests are disabled by default, since the memgraph instance is
# neccessary to execute them.
option(BUILD_TESTING_INTEGRATION "" OFF)
message(STATUS "BUILD_TESTING_INTEGRATION: ${BUILD_TESTING_INTEGRATION}")

# build header only cpp bindings
option(BUILD_CPP_BINDINGS "" OFF)
if (BUILD_TESTING OR BUILD_TESTING_INTEGRATION)
  set(BUILD_CPP_BINDINGS ON)
  message(STATUS "Testing triggering cpp binding dependancy.")
endif()

message(STATUS "BUILD_CPP_BINDINGS: ${BUILD_CPP_BINDINGS}")

include(CTest)

set(CMAKE_C_STANDARD 11)
# C++17 is fine here because it is required only for the testing purposes.
set(CMAKE_CXX_STANDARD 17)
set(C_STANDARD_REQUIRED ON)
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Link these "libraries" to set compiler warnings on targets.
include(cmake/CompilerWarnings.cmake)
add_library(project_c_warnings INTERFACE)
set_project_c_warnings(project_c_warnings)
add_library(project_cpp_warnings INTERFACE)
set_project_cpp_warnings(project_cpp_warnings)

if(EMSCRIPTEN)
   # same as project_c_warnings but without -O3. In the future we should
   # experiment and switch to O3. because it reduces the js output size
   # significantly.
   set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic -std=gnu11")
   set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG -Wall -Wextra -Wpedantic -std=gnu11")
endif()

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

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src)

if(BUILD_CPP_BINDINGS)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/mgclient_cpp)
endif()

if(BUILD_TESTING)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
