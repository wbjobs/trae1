cmake_minimum_required(VERSION 3.16)
project(audio_fft)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EMSDK "$ENV{EMSDK}")
if(NOT EMSDK)
    message(FATAL_ERROR "EMSDK environment variable not set. Please install Emscripten SDK.")
endif()

set(CMAKE_C_COMPILER "${EMSDK}/upstream/emscripten/emcc")
set(CMAKE_CXX_COMPILER "${EMSDK}/upstream/emscripten/em++")

add_executable(fft fft.cpp)

set_target_properties(fft PROPERTIES
    SUFFIX ".js"
    LINK_FLAGS "\
        -lembind \
        -s WASM=1 \
        -s EXPORT_ES6=1 \
        -s MODULARIZE=1 \
        -s EXPORT_NAME=FFTModule \
        -s ENVIRONMENT=web,worker \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s MAXIMUM_MEMORY=512MB \
        -O3 \
        --bind \
    "
)

set_target_properties(fft PROPERTIES
    OUTPUT_NAME "fft"
)
