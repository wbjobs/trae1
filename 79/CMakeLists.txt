cmake_minimum_required(VERSION 3.15)
project(usb_flash C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W3 /O2)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

option(USE_LIBSODIUM "Use libsodium for Ed25519 signatures" OFF)

if(USE_LIBSODIUM)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SODIUM libsodium)
    endif()

    if(NOT SODIUM_FOUND)
        find_path(SODIUM_INCLUDE_DIR
            NAMES sodium.h
        )
        find_library(SODIUM_LIBRARY
            NAMES sodium
        )
        if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
            set(SODIUM_FOUND TRUE)
            set(SODIUM_INCLUDE_DIRS ${SODIUM_INCLUDE_DIR})
            set(SODIUM_LIBRARIES ${SODIUM_LIBRARY})
        endif()
    endif()

    if(SODIUM_FOUND)
        add_compile_definitions(USE_LIBSODIUM)
        include_directories(${SODIUM_INCLUDE_DIRS})
        message(STATUS "Found libsodium: ${SODIUM_LIBRARIES}")
    else()
        message(WARNING "libsodium not found, using built-in Ed25519 implementation")
    endif()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBUSB libusb-1.0)
endif()

if(NOT LIBUSB_FOUND)
    find_path(LIBUSB_INCLUDE_DIR
        NAMES libusb.h
        PATH_SUFFIXES libusb-1.0
    )
    find_library(LIBUSB_LIBRARY
        NAMES usb-1.0 libusb-1.0
    )
    if(LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARY)
        set(LIBUSB_FOUND TRUE)
        set(LIBUSB_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIR})
        set(LIBUSB_LIBRARIES ${LIBUSB_LIBRARY})
    endif()
endif()

if(NOT LIBUSB_FOUND)
    message(FATAL_ERROR "libusb-1.0 not found. Please install libusb development package.")
endif()

include_directories(${LIBUSB_INCLUDE_DIRS})

set(SOURCES
    src/main.c
    src/logger.c
    src/progress.c
    src/firmware.c
    src/usb_handler.c
    src/config.c
    src/sign.c
    src/suit.c
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} ${LIBUSB_LIBRARIES} pthread)

if(USE_LIBSODIUM AND SODIUM_FOUND)
    target_link_libraries(${PROJECT_NAME} ${SODIUM_LIBRARIES})
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
