cmake_minimum_required(VERSION 3.20)
project(BDFR_PerformanceCapture VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BDFRPC_BUILD_TOOLS "Build diagnostic tools" ON)
option(BDFRPC_BUILD_TESTS "Build tests" ON)
option(BDFRPC_ENABLE_OPENCV "Enable live UVC/OpenCV camera capture" OFF)
option(BDFRPC_BUILD_STUDIO "Build the Qt BDFR Performance Studio application" OFF)

add_library(bdfrpc_core
    src/CaptureManager.cpp
    src/FrameSynchronizer.cpp
    src/FrameFanout.cpp
    src/SourceRouter.cpp
    src/FusionCore.cpp
    src/LatestFrameStore.cpp
    src/FusionRuntime.cpp
    src/AdapterProtocol.cpp
    src/ClockSync.cpp
    src/DeviceDiscovery.cpp
    src/Calibration.cpp
    src/SolverAdapters.cpp
    src/SourceHealth.cpp
    src/TakeRecorder.cpp
    src/TakeReader.cpp
    src/BinaryTake.cpp
    src/TakeSessionIndex.cpp
    src/Skeleton.cpp
    src/Retarget.cpp
    src/Kinematics.cpp
    src/BvhExport.cpp
    src/BDFRFacialUdpSource.cpp
    src/UnrealFacialUdpSender.cpp
    src/EasyMocapTcpSource.cpp
)

target_include_directories(bdfrpc_core PUBLIC include)

if(MSVC)
    target_compile_options(bdfrpc_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(bdfrpc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(bdfrpc_core PRIVATE ws2_32)
endif()

if(BDFRPC_ENABLE_OPENCV)
    find_package(Threads REQUIRED)
    find_package(OpenCV REQUIRED COMPONENTS core videoio imgproc calib3d)
    add_library(bdfrpc_opencv
        src/OpenCVCameraSource.cpp
        src/OpenCVDeviceEnumerator.cpp
    )
    target_include_directories(bdfrpc_opencv PUBLIC include)
    target_link_libraries(bdfrpc_opencv PUBLIC bdfrpc_core Threads::Threads ${OpenCV_LIBS})
    target_compile_definitions(bdfrpc_opencv PUBLIC BDFRPC_HAS_OPENCV=1)

    if(BDFRPC_BUILD_TOOLS)
        add_executable(bdfrpc_camera_probe tools/bdfrpc_camera_probe.cpp)
        target_link_libraries(bdfrpc_camera_probe PRIVATE bdfrpc_opencv)

        add_executable(bdfrpc_devices tools/bdfrpc_devices.cpp)
        target_link_libraries(bdfrpc_devices PRIVATE bdfrpc_opencv)

        add_executable(bdfrpc_multicam_probe tools/bdfrpc_multicam_probe.cpp)
        target_link_libraries(bdfrpc_multicam_probe PRIVATE bdfrpc_opencv)

        add_executable(bdfrpc_calibrate_camera tools/bdfrpc_calibrate_camera.cpp)
        target_link_libraries(bdfrpc_calibrate_camera PRIVATE bdfrpc_opencv)

        add_executable(bdfrpc_calibrate_rig tools/bdfrpc_calibrate_rig.cpp)
        target_link_libraries(bdfrpc_calibrate_rig PRIVATE bdfrpc_opencv)
    endif()
endif()

if(BDFRPC_BUILD_TOOLS)
    add_executable(bdfrpc_probe tools/bdfrpc_probe.cpp)
    target_link_libraries(bdfrpc_probe PRIVATE bdfrpc_core)

    add_executable(bdfrpc_facial_udp_probe tools/bdfrpc_facial_udp_probe.cpp)
    target_link_libraries(bdfrpc_facial_udp_probe PRIVATE bdfrpc_core)

    add_executable(bdfrpc_easymocap_probe tools/bdfrpc_easymocap_probe.cpp)
    target_link_libraries(bdfrpc_easymocap_probe PRIVATE bdfrpc_core)

    add_executable(bdfrpc_export_bvh tools/bdfrpc_export_bvh.cpp)
    target_link_libraries(bdfrpc_export_bvh PRIVATE bdfrpc_core)

    add_executable(bdfrpc_benchmark tools/bdfrpc_benchmark.cpp)
    target_link_libraries(bdfrpc_benchmark PRIVATE bdfrpc_core)
endif()

if(BDFRPC_BUILD_TESTS)
    enable_testing()
    add_executable(bdfrpc_core_tests tests/test_core.cpp)
    target_link_libraries(bdfrpc_core_tests PRIVATE bdfrpc_core)
    add_test(NAME bdfrpc_core_tests COMMAND bdfrpc_core_tests)
endif()

if(BDFRPC_BUILD_STUDIO)
    if(NOT BDFRPC_ENABLE_OPENCV)
        message(FATAL_ERROR "BDFRPC_BUILD_STUDIO requires BDFRPC_ENABLE_OPENCV=ON")
    endif()
    add_subdirectory(apps/BDFRPerformanceStudio)
endif()
