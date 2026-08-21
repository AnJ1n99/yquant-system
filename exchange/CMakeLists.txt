# 打印完整编译命令，便于在远端构建机上排查 flag 问题
set(CMAKE_VERBOSE_MAKEFILE on)

# 收集三个子模块（matcher / market_data / order_manager）的实现文件。
# 只匹配子目录下的 .cc：顶层的 exchange_main.cpp 是进程入口，单独成目标。
file(GLOB SOURCES "*/*.cc")

# 撮合、行情、订单管理打包为一个静态库，供入口程序和将来的测试共用
add_library(libexchange STATIC ${SOURCES})

# PUBLIC：本库按 C++23 编译，链接它的目标也自动获得同一标准要求
target_compile_features(libexchange PUBLIC cxx_std_23)

# PRIVATE：警告即错误只约束本库自身的编译，不强加给使用者
target_compile_options(libexchange PRIVATE
    -Wall -Wextra -Werror -Wpedantic
)

# PUBLIC：头文件搜索路径向使用者传播，因此 exchange_main 无需重复声明
target_include_directories(libexchange PUBLIC
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/exchange
)

# Logger 与各组件都会创建线程，PUBLIC 让链接方一并拿到 -pthread
find_package(Threads REQUIRED)
target_link_libraries(libexchange PUBLIC Threads::Threads)

# 交易所进程入口
add_executable(exchange_main exchange_main.cpp)

# libexchange 提供撮合与行情，common 提供 Logger 和 socket 实现。
# 两者的 PUBLIC 属性（C++23、include 路径、pthread）在此自动继承。
target_link_libraries(exchange_main PRIVATE libexchange common)

# 入口程序沿用与库一致的告警强度
target_compile_options(exchange_main PRIVATE
    -Wall -Wextra -Werror -Wpedantic
)
