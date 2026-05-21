cmake_minimum_required(VERSION 3.10)
project(QuoteAPI)

set(CMAKE_CXX_STANDARD 17)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(PANGO REQUIRED pango pangocairo)

# Crow is header-only, but needs some flags
find_package(Threads REQUIRED)

# Include directories
include_directories(
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
    src
)

# Source files
set(SOURCES
    src/main.cpp
    src/api_handler.cpp
    src/renderer.cpp
    src/text_engine.cpp
    src/tg_client.cpp
)

add_executable(QuoteAPI ${SOURCES})

# Link libraries
target_link_libraries(QuoteAPI
    ${CAIRO_LIBRARIES}
    ${PANGO_LIBRARIES}
    Threads::Threads
    tdjson
)
