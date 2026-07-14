cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

add_library(TestUtilities STATIC
    TestUtilities.cpp
)

add_executable(string_utils_tests
    StringUtilitiesTests.cpp
    ../src/StringUtilities.cpp
)

target_include_directories(string_utils_tests PRIVATE ../src)
target_link_libraries(string_utils_tests PRIVATE TestUtilities)

add_test(NAME string_utils_tests COMMAND string_utils_tests)

add_executable(file_utils_tests
    FileUtilitiesTests.cpp
    ../src/FileUtilities.cpp
)

target_include_directories(file_utils_tests PRIVATE ../src)
target_link_libraries(file_utils_tests PRIVATE TestUtilities)

add_test(NAME file_utils_tests COMMAND file_utils_tests)
