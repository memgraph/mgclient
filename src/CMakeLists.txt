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

add_library(project_options INTERFACE)
include(../cmake/Sanitizers.cmake)
enable_sanitizers(project_options)

set(mgclient_src_files
        mgallocator.c
        mgclient.c
        mgmessage.c
        mgsession.c
        mgsession-decoder.c
        mgsession-encoder.c
        mgtransport.c
        mgvalue.c)
if(MGCLIENT_ON_APPLE)
    list(APPEND mgclient_src_files apple/mgsocket.c)
elseif(MGCLIENT_ON_LINUX)
    list(APPEND mgclient_src_files linux/mgsocket.c)
elseif(MGCLIENT_ON_WINDOWS)
    list(APPEND mgclient_src_files windows/mgsocket.c)
else()
    message(FATAL_ERROR "Operating system undefined.")
endif()

if(EMSCRIPTEN)
    list(APPEND mgclient_src_files mgwasm.c)
    add_executable(mgclient ${mgclient_src_files})
    set_target_properties(mgclient PROPERTIES LINK_FLAGS "-s ASYNCIFY=1 -s WASM_BIGINT -s MODULARIZE -s EXPORT_NAME=\"load_mgclient\" --shared-memory --no-entry -s USE_PTHREADS=1 -s WEBSOCKET_SUBPROTOCOL=\"binary\" -s EXPORTED_RUNTIME_METHODS=\"ccall, cwrap, getValue, setValue, UTF8ToString, allocateUTF8\"")

    target_include_directories(mgclient
            PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
            PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_BINARY_DIR}")
else()
    find_package(OpenSSL REQUIRED)
    include(GenerateExportHeader)

    add_library(mgclient-static STATIC ${mgclient_src_files})

    generate_export_header(mgclient-static
            BASE_NAME "mgclient"
            EXPORT_FILE_NAME "mgclient-export.h")

    set_target_properties(mgclient-static PROPERTIES
            OUTPUT_NAME mgclient)
    target_compile_definitions(mgclient-static PUBLIC MGCLIENT_STATIC_DEFINE)
    target_include_directories(mgclient-static
            PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
            PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_BINARY_DIR}"
            "${OPENSSL_INCLUDE_DIR}")
    target_link_libraries(mgclient-static
            PRIVATE
            ${OPENSSL_LIBRARIES} project_options project_c_warnings)

    if(MGCLIENT_ON_WINDOWS)
        target_link_libraries(mgclient-static PUBLIC ws2_32)
    endif()

    add_library(mgclient-shared SHARED ${mgclient_src_files})

    generate_export_header(mgclient-shared
            BASE_NAME "mgclient"
            EXPORT_FILE_NAME "mgclient-export.h")

    set_target_properties(mgclient-shared PROPERTIES
            OUTPUT_NAME mgclient
            SOVERSION ${mgclient_SOVERSION}
            C_VISIBILITY_PRESET hidden)
    target_include_directories(mgclient-shared
            PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
            PUBLIC
            "${PROJECT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_BINARY_DIR}"
            "${OPENSSL_INCLUDE_DIR}")
    target_link_libraries(mgclient-shared
            PRIVATE
            ${OPENSSL_LIBRARIES} project_options project_c_warnings)

    if(MGCLIENT_ON_WINDOWS)
        target_link_libraries(mgclient-shared PUBLIC ws2_32)
    endif()

    generate_export_header(mgclient-shared
            BASE_NAME "mgclient"
            EXPORT_FILE_NAME "mgclient-export.h")

    include(GNUInstallDirs)

    install(TARGETS mgclient-static mgclient-shared
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY
            "${PROJECT_SOURCE_DIR}/include/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/mgclient-export.h"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
