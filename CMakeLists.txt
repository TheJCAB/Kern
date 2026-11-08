cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(kern LANGUAGES CXX)

enable_testing()

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

set(JSON_TARGET "")
find_package(nlohmann_json CONFIG QUIET)
if(TARGET nlohmann_json::nlohmann_json)
    set(JSON_TARGET nlohmann_json::nlohmann_json)
endif()
if(NOT JSON_TARGET)
    CPMAddPackage(URI "gh:nlohmann/json@3.12.0" OPTIONS "JSON_BuildTests OFF")
    set(JSON_TARGET nlohmann_json::nlohmann_json)
endif()

set(HTTPLIB_TARGET "")
find_package(cpp-httplib CONFIG QUIET)
if(TARGET cpphttplib::cpphttplib)
    set(HTTPLIB_TARGET cpphttplib::cpphttplib)
elseif(TARGET "cpp-httplib::cpp-httplib")
    set(HTTPLIB_TARGET "cpp-httplib::cpp-httplib")
endif()
if(NOT HTTPLIB_TARGET)
    find_package(httplib CONFIG QUIET)
    if(TARGET httplib::httplib)
        set(HTTPLIB_TARGET httplib::httplib)
    endif()
endif()
if(NOT HTTPLIB_TARGET)
    CPMAddPackage(NAME httplib GITHUB_REPOSITORY yhirose/cpp-httplib VERSION 0.51.0)
    set(HTTPLIB_TARGET httplib::httplib)
endif()

add_executable(kern
    src/FileUtilities.cpp       src/FileUtilities.h
    src/FileValidation.cpp      src/FileValidation.h
    src/NetworkUtilities.cpp    src/NetworkUtilities.h
    src/StringUtilities.cpp     src/StringUtilities.h
    src/ToolUtilities.cpp       src/ToolUtilities.h

    src/Session.cpp         src/Session.h

    src/Tools/edit_file_lines.h
    src/Tools/glob.h
    src/Tools/grep.h
    src/Tools/read_file.h
    src/Tools/read_file_chunk.h
    src/Tools/write_file.h

    src/main.cpp
)

target_compile_features(kern PRIVATE cxx_std_23)

target_include_directories(kern PRIVATE src)

target_link_libraries(kern PRIVATE ${JSON_TARGET} ${HTTPLIB_TARGET})

set(RUNTIME_DATA_DESTINATION $<TARGET_FILE_DIR:kern>/data)

add_custom_target(copy_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data
            ${RUNTIME_DATA_DESTINATION}
    COMMENT "Copying runtime data into the build output directory"
)

add_dependencies(kern copy_runtime_data)

add_subdirectory(tests)
