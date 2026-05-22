cmake_minimum_required(VERSION 3.10)
project(QuoteAPI)

set(CMAKE_CXX_STANDARD 17)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(PANGO REQUIRED pango pangocairo)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

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
    src/mtproto/crypto.cpp
    src/mtproto/tl.cpp
    src/mtproto/transport.cpp
    src/mtproto/auth_key.cpp
    src/mtproto/session.cpp
    src/mtproto/client.cpp
)

add_executable(QuoteAPI ${SOURCES})

# Link libraries
target_link_libraries(QuoteAPI
    ${CAIRO_LIBRARIES}
    ${PANGO_LIBRARIES}
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::system
    ZLIB::ZLIB
)
