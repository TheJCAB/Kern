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

add_executable(file_validation_tests
    FileValidationTests.cpp
    ../src/FileUtilities.cpp
    ../src/FileValidation.cpp
)

target_include_directories(file_validation_tests PRIVATE ../src)
target_link_libraries(file_validation_tests PRIVATE TestUtilities)

add_test(NAME file_validation_tests COMMAND file_validation_tests)

add_executable(services_tests
    ServicesTests.cpp
    ../src/Credentials.cpp
    ../src/Endpoint.cpp
    ../src/FileUtilities.cpp
    ../src/Services.cpp
)

target_include_directories(services_tests PRIVATE ../src)
target_link_libraries(services_tests PRIVATE TestUtilities nlohmann_json::nlohmann_json)

add_test(NAME services_tests COMMAND services_tests)

add_executable(logging_tests
    LoggingTests.cpp
    ../src/Logging.cpp
)

target_include_directories(logging_tests PRIVATE ../src)
target_link_libraries(logging_tests PRIVATE TestUtilities)

add_test(NAME logging_tests COMMAND logging_tests)

add_executable(command_line_tests
    CommandLineTests.cpp
    ../src/CommandLine.cpp
    ../src/Credentials.cpp
    ../src/Endpoint.cpp
    ../src/FileUtilities.cpp
    ../src/Logging.cpp
    ../src/Services.cpp
)

target_include_directories(command_line_tests PRIVATE ../src)
target_link_libraries(command_line_tests PRIVATE TestUtilities nlohmann_json::nlohmann_json)

add_test(NAME command_line_tests COMMAND command_line_tests)

add_executable(build_command_tests
    BuildCommandTests.cpp
    ../src/BuildCommand.cpp
    ../src/FileUtilities.cpp
    ../src/FileValidation.cpp
)

target_include_directories(build_command_tests PRIVATE ../src)
target_link_libraries(build_command_tests PRIVATE TestUtilities nlohmann_json::nlohmann_json)

add_test(NAME build_command_tests COMMAND build_command_tests)

add_executable(tool_feedback_tests
    ToolFeedbackTests.cpp
    ../src/FileUtilities.cpp
    ../src/FileValidation.cpp
    ../src/StringUtilities.cpp
)

target_include_directories(tool_feedback_tests PRIVATE ../src)
target_link_libraries(tool_feedback_tests PRIVATE TestUtilities nlohmann_json::nlohmann_json)

add_test(NAME tool_feedback_tests COMMAND tool_feedback_tests)

add_executable(session_recorder_tests
    SessionRecorderTests.cpp
    ../src/SessionRecorder.cpp
    ../src/FileUtilities.cpp
    ../src/Logging.cpp
)

target_include_directories(session_recorder_tests PRIVATE ../src)
target_link_libraries(session_recorder_tests PRIVATE TestUtilities nlohmann_json::nlohmann_json)

add_test(NAME session_recorder_tests COMMAND session_recorder_tests)