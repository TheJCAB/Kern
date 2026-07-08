cmake_minimum_required(VERSION 3.24)
project(gemma_agent_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(agent_harness
    src/FileUtilities.cpp       src/FileUtilities.h
    src/NetworkUtilities.cpp    src/NetworkUtilities.h
    src/StringUtilities.cpp     src/StringUtilities.h

    src/main.cpp
)

target_compile_features(agent_harness PRIVATE cxx_std_20)

set(RUNTIME_DATA_DESTINATION $<TARGET_FILE_DIR:agent_harness>/data)

add_custom_target(copy_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/data
            ${RUNTIME_DATA_DESTINATION}
    COMMENT "Copying runtime data into the build output directory"
)

add_dependencies(agent_harness copy_runtime_data)
