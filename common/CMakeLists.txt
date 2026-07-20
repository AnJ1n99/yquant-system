# Shared utilities: headers + network socket implementations.

add_library(common STATIC
    mcast_socket.cpp
    tcp_server.cc
    tcp_socket.cc
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Public headers for IDE / tooling visibility (not separate TUs).
target_sources(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/logging.h
    ${CMAKE_CURRENT_SOURCE_DIR}/macros.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mcast_socket.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_pool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/perf_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ringBuffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/socket_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_server.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_socket.h
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/time_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/types.h
)

target_compile_features(common PUBLIC cxx_std_20)
target_compile_options(common PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
