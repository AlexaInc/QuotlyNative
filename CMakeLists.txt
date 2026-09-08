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

# Header-only dependencies (Crow / Asio / nlohmann-json). These used to fail
# with a cryptic "asio.hpp: No such file or directory" deep inside crow.h —
# now we fail fast at configure time with the exact fix.
find_path(CROW_INCLUDE_DIR crow.h PATHS /usr/local/include /usr/include)
if(NOT CROW_INCLUDE_DIR)
    message(FATAL_ERROR "crow.h not found. Fix:\n"
            "  sudo wget -O /usr/local/include/crow.h "
            "https://github.com/CrowCpp/Crow/releases/download/v1.2.0/crow_all.h")
endif()
find_path(ASIO_INCLUDE_DIR asio.hpp PATHS /usr/include /usr/local/include)
if(NOT ASIO_INCLUDE_DIR)
    message(FATAL_ERROR "asio.hpp not found. Fix: sudo apt-get install libasio-dev")
endif()
find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp PATHS /usr/include /usr/local/include)
if(NOT NLOHMANN_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp not found. Fix: sudo apt-get install nlohmann-json3-dev")
endif()

# Include directories
include_directories(
    ${CAIRO_INCLUDE_DIRS}
    ${PANGO_INCLUDE_DIRS}
    ${CROW_INCLUDE_DIR}
    ${ASIO_INCLUDE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
    src
)

# Source files
set(SOURCES
    src/main.cpp
    src/api_handler.cpp
    src/renderer.cpp
    src/image_decode.cpp
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
