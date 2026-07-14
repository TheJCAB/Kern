cmake_minimum_required(VERSION 3.24)

enable_testing()

add_executable(string_utils_tests
    StringUtilitiesTests.cpp
    ../src/StringUtilities.cpp
)    

target_include_directories(string_utils_tests PRIVATE ../src)
target_link_libraries(string_utils_tests PRIVATE nlohmann_json::nlohmann_json)

add_test(string_utils_tests COMMAND string_utils_tests)
