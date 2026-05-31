cmake_minimum_required(VERSION 3.10)
project(vnc_watermark_proxy C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBVNCSERVER REQUIRED libvncserver)
pkg_check_modules(LIBVNCCLIENT REQUIRED libvncclient)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(TESSERACT REQUIRED tesseract)
pkg_check_modules(LEPTONICA REQUIRED lept)
pkg_check_modules(LIBCURL REQUIRED libcurl)

include_directories(
    ${LIBVNCSERVER_INCLUDE_DIRS}
    ${LIBVNCCLIENT_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${TESSERACT_INCLUDE_DIRS}
    ${LEPTONICA_INCLUDE_DIRS}
    ${LIBCURL_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)

link_directories(
    ${LIBVNCSERVER_LIBRARY_DIRS}
    ${LIBVNCCLIENT_LIBRARY_DIRS}
    ${SQLITE3_LIBRARY_DIRS}
    ${OPENSSL_LIBRARY_DIRS}
    ${TESSERACT_LIBRARY_DIRS}
    ${LEPTONICA_LIBRARY_DIRS}
    ${LIBCURL_LIBRARY_DIRS}
)

add_executable(vnc_proxy
    src/main.c
    src/proxy.c
    src/watermark.c
    src/logger.c
    src/recorder.c
    src/tls.c
    src/sensitive_words.c
    src/ocr_engine.c
    src/email_alert.c
)

target_link_libraries(vnc_proxy
    ${LIBVNCSERVER_LIBRARIES}
    ${LIBVNCCLIENT_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${TESSERACT_LIBRARIES}
    ${LEPTONICA_LIBRARIES}
    ${LIBCURL_LIBRARIES}
    pthread
    z
    jpeg
    png
)

target_compile_options(vnc_proxy PRIVATE -Wall -Wextra -O2)
