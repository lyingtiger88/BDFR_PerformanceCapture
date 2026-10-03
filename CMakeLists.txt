cmake_minimum_required(VERSION 3.20)
project(BDFR_PerformanceCapture VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BDFRPC_BUILD_TOOLS "Build diagnostic tools" ON)
option(BDFRPC_BUILD_TESTS "Build tests" ON)

add_library(bdfrpc_core
    src/CaptureManager.cpp
    src/FrameSynchronizer.cpp
    src/SourceRouter.cpp
    src/FusionCore.cpp
    src/AdapterProtocol.cpp
)

target_include_directories(bdfrpc_core PUBLIC include)

if(MSVC)
    target_compile_options(bdfrpc_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(bdfrpc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BDFRPC_BUILD_TOOLS)
    add_executable(bdfrpc_probe tools/bdfrpc_probe.cpp)
    target_link_libraries(bdfrpc_probe PRIVATE bdfrpc_core)
endif()

if(BDFRPC_BUILD_TESTS)
    enable_testing()
    add_executable(bdfrpc_core_tests tests/test_core.cpp)
    target_link_libraries(bdfrpc_core_tests PRIVATE bdfrpc_core)
    add_test(NAME bdfrpc_core_tests COMMAND bdfrpc_core_tests)
endif()
