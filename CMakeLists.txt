cmake_minimum_required(VERSION 3.25)
project(OpenFrameMeter VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
option(FRAME_METER_CORE_ONLY "Build standalone counter tests without Geode" OFF)
if(FRAME_METER_CORE_ONLY)
    enable_testing()
    add_executable(counter_tests tests/counter_tests.cpp)
    add_test(NAME counter_tests COMMAND counter_tests)
    add_executable(analysis_tests tests/analysis_tests.cpp)
    add_test(NAME analysis_tests COMMAND analysis_tests)
    add_executable(replay_lifecycle_tests tests/replay_lifecycle_tests.cpp)
    add_test(NAME replay_lifecycle_tests COMMAND replay_lifecycle_tests)
    add_executable(physics_clock_tests tests/physics_clock_tests.cpp)
    add_test(NAME physics_clock_tests COMMAND physics_clock_tests)
    add_executable(gdr_tests tests/gdr_tests.cpp)
    add_test(NAME gdr_tests COMMAND gdr_tests)
    add_executable(macro_formats_tests tests/macro_formats_tests.cpp)
    add_test(NAME macro_formats_tests COMMAND macro_formats_tests)
    set(FRAME_METER_GDR2_REFERENCE_DIR "" CACHE PATH "Optional official GDR2 headers directory, containing gdr.hpp and binarystream.hpp")
    if(FRAME_METER_GDR2_REFERENCE_DIR)
        add_executable(gdr2_interop_tests tests/gdr2_interop_tests.cpp)
        target_include_directories(gdr2_interop_tests PRIVATE "${FRAME_METER_GDR2_REFERENCE_DIR}")
        add_test(NAME gdr2_interop_tests COMMAND gdr2_interop_tests)
    endif()
    add_executable(ui_layout_tests tests/ui_layout_tests.cpp)
    add_test(NAME ui_layout_tests COMMAND ui_layout_tests)
    if(WIN32)
        add_executable(encoder_discovery_tests tests/encoder_discovery_tests.cpp)
        add_test(NAME encoder_discovery_tests COMMAND encoder_discovery_tests)
        add_executable(video_backend_tests tests/video_backend_tests.cpp src/VideoCapture.cpp)
        target_link_libraries(video_backend_tests ole32 mmdevapi)
        add_test(NAME video_backend_tests COMMAND video_backend_tests)
    endif()
    return()
endif()
if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Set GEODE_SDK to the Geode 5.10.1 SDK directory")
endif()
add_library(${PROJECT_NAME} SHARED src/main.cpp src/VideoCapture.cpp)
if(MSVC AND MSVC_VERSION EQUAL 1944)
    # Work around MSVC 19.44 optimizer crashes in the SDK async/modify-heavy UI translation unit.
    set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS "/Od;/Ob0" SKIP_PRECOMPILE_HEADERS ON)
endif()
add_subdirectory("$ENV{GEODE_SDK}" "${CMAKE_CURRENT_BINARY_DIR}/geode")
target_link_libraries(${PROJECT_NAME} ole32 mmdevapi)
setup_geode_mod(${PROJECT_NAME} DONT_INSTALL)










