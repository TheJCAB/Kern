cmake_minimum_required(VERSION 3.24)
project(gemma_agent_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(agent_harness src/main.cpp)
target_compile_features(agent_harness PRIVATE cxx_std_20)
