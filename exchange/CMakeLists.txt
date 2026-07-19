set(CMAKE_VERBOSE_MAKEFILE on)

file(GLOB SOURCES "*/*.cc")

# order_manager/order_manager.cc depends on Linux-only epoll (via tcp_server.h).
# Exclude it on non-Linux platforms so the rest of the library still builds.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(FILTER SOURCES EXCLUDE REGEX ".*/order_manager/order_manager\\.cc$")
endif()

add_library(libexchange STATIC ${SOURCES})

target_compile_features(libexchange PUBLIC cxx_std_20)
target_compile_options(libexchange PRIVATE
    -Wall -Wextra -Werror -Wpedantic
)
target_include_directories(libexchange PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/exchange
)
