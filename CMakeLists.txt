cmake_minimum_required(VERSION 3.24)
project(kern LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_executable(kern
    src/FileUtilities.cpp       src/FileUtilities.h
    src/NetworkUtilities.cpp    src/NetworkUtilities.h
    src/StringUtilities.cpp     src/StringUtilities.h
    src/ToolUtilities.cpp       src/ToolUtilities.h

    src/Tools/read_file.h
    src/Tools/read_file_chunk.h
    src/Tools/write_file.h

    src/main.cpp
)

target_compile_features(kern PRIVATE cxx_std_20)

target_include_directories(kern PRIVATE src)

target_link_libraries(kern PRIVATE nlohmann_json::nlohmann_json)

set(RUNTIME_DATA_DESTINATION $<TARGET_FILE_DIR:kern>/data)

add_custom_target(copy_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data
            ${RUNTIME_DATA_DESTINATION}
    COMMENT "Copying runtime data into the build output directory"
)

add_dependencies(kern copy_runtime_data)
