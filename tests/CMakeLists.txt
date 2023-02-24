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

find_package(Threads REQUIRED)
include(FetchContent)

set(GTEST_GIT_TAG "release-1.8.1" CACHE STRING "GTest git tag")
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        ${GTEST_GIT_TAG}
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(TESTS_ROOT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
macro(add_gtest target_name target_path)
  add_executable(${target_name} ${target_path})
  target_include_directories(${target_name} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${TESTS_ROOT_DIRECTORY}")
  target_link_libraries(${target_name} mgclient-static mgclient_cpp gtest gtest_main gmock_main project_cpp_warnings)
  if (ENABLE_COVERAGE)
    add_test(${target_name} env LLVM_PROFILE_FILE=${target_name}.profraw ./${target_name})
  else()
    add_test(${target_name} ${target_name})
  endif()
endmacro()

add_gtest(value value.cpp)
add_gtest(encoder encoder.cpp)
add_gtest(decoder decoder.cpp)
add_gtest(client client.cpp)
# We're mocking the mg_secure_transport_init function in the test.
if(MGCLIENT_ON_APPLE)
    target_link_libraries(client -Wl,-alias,___wrap_mg_secure_transport_init,_mg_secure_transport_init)
elseif(MGCLIENT_ON_LINUX)
    target_link_libraries(client -Wl,--wrap=mg_secure_transport_init)
endif()
add_gtest(transport transport.cpp)
if(MGCLIENT_ON_APPLE)
    target_link_libraries(transport c++)
else()
    target_link_libraries(transport stdc++fs)
endif()
add_gtest(allocator allocator.cpp)
add_gtest(unit_mgclient_value unit/mgclient_value.cpp)

if(BUILD_TESTING_INTEGRATION)
  add_gtest(integration_basic_c integration/basic_c.cpp)
  add_gtest(integration_basic_cpp integration/basic_cpp.cpp)
endif()

# Build examples and add them to tests
set(EXAMPLE_DIR ${PROJECT_SOURCE_DIR}/examples)

add_executable(example_basic_c ${EXAMPLE_DIR}/basic.c)
target_link_libraries(example_basic_c mgclient-static project_c_warnings)
add_test(example_basic_c example_basic_c 127.0.0.1 7687 "RETURN 1")

add_executable(example_basic_cpp ${EXAMPLE_DIR}/basic.cpp)
target_link_libraries(example_basic_cpp mgclient-static mgclient_cpp project_cpp_warnings)
add_test(example_basic_cpp example_basic_cpp 127.0.0.1 7687 "RETURN 1")

add_executable(example_advanced_cpp ${EXAMPLE_DIR}/advanced.cpp)
target_link_libraries(example_advanced_cpp mgclient-static mgclient_cpp project_cpp_warnings)
add_test(example_advanced_cpp example_advanced_cpp 127.0.0.1 7687)
