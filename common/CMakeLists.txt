# common 库的 CMakeLists.txt

# 收集所有头文件
set(COMMON_HEADERS
    macros.h
    ringBuffer_queue.h
)

# 由于这些都是头文件库，我们可以创建一个接口库
add_library(common INTERFACE)

target_include_directories(common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 如果将来有 .cpp 文件，可以改为：
# add_library(common STATIC
#     file1.cpp
#     file2.cpp
# )

# target_include_directories(common PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}
# )
