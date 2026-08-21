# Shared utilities: headers + Linux network socket implementations.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(common STATIC
        mcast_socket.cpp
        tcp_server.cc
        tcp_socket.cc
    )
    set(COMMON_USAGE_SCOPE PUBLIC)
    target_compile_options(common PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
else()
    # Non-Linux development builds expose portable headers only. The network
    # implementations depend on Linux epoll and socket behavior.
    add_library(common INTERFACE)
    set(COMMON_USAGE_SCOPE INTERFACE)
endif()

target_include_directories(common ${COMMON_USAGE_SCOPE}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Public headers for IDE / tooling visibility (not separate TUs).
target_sources(common ${COMMON_USAGE_SCOPE}
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

target_compile_features(common ${COMMON_USAGE_SCOPE} cxx_std_23)
