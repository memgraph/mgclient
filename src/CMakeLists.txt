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

set(mgclient_src_files
        mgallocator.c
        mgclient.c
        mgmessage.c
        mgsession.c
        mgsession-decoder.c
        mgsession-encoder.c
        mgtransport.c
        mgvalue.c)
if(${WIN32})
    list(APPEND mgclient_src_files mgsocket-windows.c)
else()
    list(APPEND mgclient_src_files mgsocket-posix.c)
endif()

find_package(OpenSSL REQUIRED)

include(GenerateExportHeader)

add_library(mgclient-static STATIC ${mgclient_src_files})
set_target_properties(mgclient-static PROPERTIES
        OUTPUT_NAME mgclient)
target_compile_definitions(mgclient-static PUBLIC MGCLIENT_STATIC_DEFINE)
target_include_directories(mgclient-static
        PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${OPENSSL_INCLUDE_DIR}")
target_link_libraries(mgclient-static PRIVATE ${OPENSSL_LIBRARIES})
if(${WIN32})
    target_link_libraries(mgclient-static PUBLIC ws2_32)
endif()

add_library(mgclient-shared SHARED ${mgclient_src_files})
set_target_properties(mgclient-shared PROPERTIES
        OUTPUT_NAME mgclient
        C_VISIBILITY_PRESET hidden)
target_include_directories(mgclient-shared
        PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${OPENSSL_INCLUDE_DIR}")
target_link_libraries(mgclient-shared PRIVATE ${OPENSSL_LIBRARIES})
if(${WIN32})
    target_link_libraries(mgclient-shared PUBLIC ws2_32)
endif()

generate_export_header(mgclient-shared
        BASE_NAME "mgclient"
        EXPORT_FILE_NAME "mgclient-export.h")

include(GNUInstallDirs)

install(TARGETS mgclient-static mgclient-shared
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/mgclient-export.h"
        "${PROJECT_SOURCE_DIR}/include/mgclient.h"
        "${PROJECT_SOURCE_DIR}/include/mgclient-error.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
