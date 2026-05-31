cmake_minimum_required(VERSION 3.15)
project(AudioFingerprint VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

option(ENABLE_STREAMING "Enable streaming audio recognition" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SOUNDIO REQUIRED libsoundio)
pkg_check_modules(FFTW3 REQUIRED fftw3)

find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
find_library(SQLITE3_LIBRARY sqlite3)

if(NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
    message(FATAL_ERROR "SQLite3 not found")
endif()

set(STREAMING_SOURCES)
set(STREAMING_LIBS)

if(ENABLE_STREAMING)
    pkg_check_modules(AVFORMAT libavformat)
    pkg_check_modules(AVCODEC libavcodec)
    pkg_check_modules(AVUTIL libavutil)
    pkg_check_modules(SWRESAMPLE libswresample)

    if(AVFORMAT_FOUND AND AVCODEC_FOUND AND AVUTIL_FOUND AND SWRESAMPLE_FOUND)
        message(STATUS "Streaming support: ENABLED")
        add_definitions(-DENABLE_STREAMING)
        set(STREAMING_SOURCES src/stream_input.cpp src/segment_manager.cpp src/srt_writer.cpp)
        set(STREAMING_LIBS ${AVFORMAT_LIBRARIES} ${AVCODEC_LIBRARIES}
                          ${AVUTIL_LIBRARIES} ${SWRESAMPLE_LIBRARIES})
        include_directories(${AVFORMAT_INCLUDE_DIRS} ${AVCODEC_INCLUDE_DIRS}
                           ${AVUTIL_INCLUDE_DIRS} ${SWRESAMPLE_INCLUDE_DIRS})
        link_directories(${AVFORMAT_LIBRARY_DIRS} ${AVCODEC_LIBRARY_DIRS}
                        ${AVUTIL_LIBRARY_DIRS} ${SWRESAMPLE_LIBRARY_DIRS})
    else()
        message(STATUS "Streaming support: DISABLED (FFmpeg not found)")
        set(ENABLE_STREAMING OFF)
    endif()
else()
    message(STATUS "Streaming support: DISABLED")
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${SOUNDIO_INCLUDE_DIRS}
    ${FFTW3_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIR}
)

link_directories(
    ${SOUNDIO_LIBRARY_DIRS}
    ${FFTW3_LIBRARY_DIRS}
)

set(CORE_SOURCES
    src/audio_capture.cpp
    src/preprocessor.cpp
    src/fingerprint.cpp
    src/database.cpp
    src/matcher.cpp
    ${STREAMING_SOURCES}
)

add_library(afp_core STATIC ${CORE_SOURCES})
target_link_libraries(afp_core
    ${SOUNDIO_LIBRARIES}
    ${FFTW3_LIBRARIES}
    ${SQLITE3_LIBRARY}
    ${STREAMING_LIBS}
    pthread
    m
)

add_executable(afp_cli src/main.cpp)
target_link_libraries(afp_cli afp_core)

add_executable(afp_builddb tools/build_db.cpp)
target_link_libraries(afp_builddb afp_core)

install(TARGETS afp_cli afp_builddb DESTINATION bin)
install(FILES include/audio_capture.hpp include/preprocessor.hpp
        include/fingerprint.hpp include/database.hpp include/matcher.hpp
        include/common.hpp DESTINATION include/afp)

if(ENABLE_STREAMING)
    install(FILES include/stream_input.hpp include/segment_manager.hpp
            include/srt_writer.hpp DESTINATION include/afp)
endif()
