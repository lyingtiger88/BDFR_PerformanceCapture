find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(BDFRPerformanceStudio
    main.cpp
    PerformanceMainWindow.h
    PerformanceMainWindow.cpp
    SkeletonViewportWidget.h
    SkeletonViewportWidget.cpp
)

target_link_libraries(BDFRPerformanceStudio PRIVATE
    bdfrpc_core
    bdfrpc_opencv
    Qt6::Widgets
)

target_include_directories(BDFRPerformanceStudio PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(BDFRPerformanceStudio PRIVATE
    BDFRPC_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

set_target_properties(BDFRPerformanceStudio PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    OUTPUT_NAME "BDFR Performance Studio"
)

add_test(
    NAME bdfrpc_studio_self_test
    COMMAND BDFRPerformanceStudio --self-test
)
