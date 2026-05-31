cmake_minimum_required(VERSION 3.16)
project(VideoSRService VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

option(USE_HLS_SIMULATION "Enable Vitis HLS xsim software emulation" ON)
option(USE_CPU_REFERENCE "Enable CPU reference implementation (fallback)" ON)
option(ENABLE_MOTION_COMPENSATION "Enable temporal motion compensation with 3D convolution" ON)
option(ENABLE_SCENE_CLASSIFICATION "Enable scene-based adaptive model switching" ON)

if(USE_HLS_SIMULATION)
    add_definitions(-DUSE_HLS_SIMULATION)
endif()

if(ENABLE_MOTION_COMPENSATION)
    add_definitions(-DENABLE_MOTION_COMPENSATION)
endif()

if(ENABLE_SCENE_CLASSIFICATION)
    add_definitions(-DENABLE_SCENE_CLASSIFICATION)
endif()

set(SOURCES
    src/main.cpp
    src/edsr/edsr_model.cpp
    src/edsr/edsr_quantized.cpp
    src/edsr/edsr_weights_loader.cpp
    src/hls/hls_wrapper.cpp
    src/hls/xsim_emulator.cpp
    src/rtmp/rtmp_receiver.cpp
    src/pipeline/stream_pipeline.cpp
    src/pipeline/pipeline_manager.cpp
    src/metrics/fps_counter.cpp
    src/metrics/psnr_evaluator.cpp
    src/metrics/performance_dashboard.cpp
    src/motion/motion_compensation.cpp
    src/motion/temporal_fusion.cpp
    src/motion/artifact_detector.cpp
    src/scene/scene_classifier.cpp
    src/scene/scene_model_manager.cpp
    src/fpga/fpga_bitstream_loader.cpp
    src/utils/args_parser.cpp
)

set(HEADERS
    src/edsr/edsr_model.h
    src/edsr/edsr_quantized.h
    src/edsr/edsr_weights_loader.h
    src/hls/hls_wrapper.h
    src/hls/xsim_emulator.h
    src/rtmp/rtmp_receiver.h
    src/pipeline/stream_pipeline.h
    src/pipeline/pipeline_manager.h
    src/metrics/fps_counter.h
    src/metrics/psnr_evaluator.h
    src/metrics/performance_dashboard.h
    src/motion/motion_compensation.h
    src/motion/temporal_fusion.h
    src/motion/artifact_detector.h
    src/scene/scene_classifier.h
    src/scene/scene_model_manager.h
    src/fpga/fpga_bitstream_loader.h
    src/utils/args_parser.h
    src/utils/common.h
)

add_executable(video_sr_service ${SOURCES} ${HEADERS})

target_include_directories(video_sr_service PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(video_sr_service PRIVATE
    ${OpenCV_LIBS}
    Threads::Threads
)

if(WIN32)
    target_link_libraries(video_sr_service PRIVATE ws2_32)
endif()

target_compile_options(video_sr_service PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

install(TARGETS video_sr_service DESTINATION bin)

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "HLS Simulation: ${USE_HLS_SIMULATION}")
message(STATUS "CPU Reference: ${USE_CPU_REFERENCE}")
message(STATUS "Motion Compensation: ${ENABLE_MOTION_COMPENSATION}")