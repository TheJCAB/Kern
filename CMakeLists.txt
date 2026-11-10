cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(kern LANGUAGES CXX)

enable_testing()

include(GNUInstallDirs)

if(MSVC)
    add_compile_options(/W4 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# download CPM.cmake
file(
    DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.43.1/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=1c40fc102ce9625d7de7eb14f541cab30cc3138dca627f0b0ec40293ce6c2934
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

CPMAddPackage(URI "gh:nlohmann/json@3.12.0" OPTIONS "JSON_BuildTests OFF")

# HTTPS is mandatory: API keys must never travel over unencrypted connections to remote services.
CPMAddPackage(NAME httplib GITHUB_REPOSITORY yhirose/cpp-httplib VERSION 0.51.0 OPTIONS "HTTPLIB_REQUIRE_OPENSSL ON" "HTTPLIB_INSTALL OFF")

add_executable(kern
    src/BuildCommand.cpp        src/BuildCommand.h
    src/Credentials.cpp         src/Credentials.h
    src/CommandLine.cpp         src/CommandLine.h
    src/Endpoint.cpp            src/Endpoint.h
    src/FileUtilities.cpp       src/FileUtilities.h
    src/FileValidation.cpp      src/FileValidation.h
    src/Logging.cpp             src/Logging.h
    src/NetworkUtilities.cpp    src/NetworkUtilities.h
    src/RunProcess.cpp          src/RunProcess.h
    src/RuntimePaths.cpp        src/RuntimePaths.h
    src/Services.cpp            src/Services.h
    src/StringUtilities.cpp     src/StringUtilities.h
    src/ToolUtilities.cpp       src/ToolUtilities.h
    src/XmlToolCallParser.cpp   src/XmlToolCallParser.h

    src/ChatTransport.cpp  src/ChatTransport.h
    src/Conversation.cpp    src/Conversation.h
    src/LogFile.cpp         src/LogFile.h
    src/ModelResponse.cpp   src/ModelResponse.h
    src/Session.cpp         src/Session.h
    src/SessionRecorder.cpp src/SessionRecorder.h
    src/TokenLimitRecovery.cpp src/TokenLimitRecovery.h
    src/TurnRecord.cpp      src/TurnRecord.h
    src/Tools/build.h
    src/Tools/edit_file_lines.h
    src/Tools/glob.h
    src/Tools/grep.h
    src/Tools/implement.h
    src/Tools/read_file.h
    src/Tools/read_file_chunk.h
    src/Tools/research.h
    src/Tools/write_file.h

    src/main.cpp
)

target_compile_features(kern PRIVATE cxx_std_23)

target_include_directories(kern PRIVATE src)

target_link_libraries(kern PRIVATE nlohmann_json::nlohmann_json httplib::httplib)

# Where the installed program keeps its runtime data (system prompts), under the install prefix.
set(KERN_INSTALL_DATA_SUBDIRECTORY ${CMAKE_INSTALL_DATADIR}/kern/data)

# The same directory expressed relative to the directory that holds the installed executable, so that no
# install prefix is ever compiled into the program and an installed tree can be moved or copied freely.
if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}" OR IS_ABSOLUTE "${CMAKE_INSTALL_DATADIR}")
    set(KERN_INSTALL_DATA_DIR_RELATIVE "")
else()
    file(RELATIVE_PATH KERN_INSTALL_DATA_DIR_RELATIVE
         "/kern-install-prefix/${CMAKE_INSTALL_BINDIR}"
         "/kern-install-prefix/${KERN_INSTALL_DATA_SUBDIRECTORY}")
endif()

target_compile_definitions(kern PRIVATE KERN_INSTALL_DATA_DIR_RELATIVE="${KERN_INSTALL_DATA_DIR_RELATIVE}")

set(RUNTIME_DATA_DESTINATION $<TARGET_FILE_DIR:kern>/data)

add_custom_target(copy_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data
            ${RUNTIME_DATA_DESTINATION}
    COMMENT "Copying runtime data into the build output directory"
)

add_dependencies(kern copy_runtime_data)

# `cmake --install . --prefix DIR` puts the program in DIR/bin and its data in DIR/share/kern/data, which is
# exactly the layout the program resolves at startup; DIR is chosen at install time and never recorded here.
install(TARGETS kern RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/ DESTINATION ${KERN_INSTALL_DATA_SUBDIRECTORY})

install(FILES ${CMAKE_SOURCE_DIR}/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

add_subdirectory(tests)
