file(GLOB SOURCES CONFIGURE_DEPENDS "*/*.cpp")

add_library(libtrading STATIC ${SOURCES})
target_compile_features(libtrading PUBLIC cxx_std_23)
target_compile_options(libtrading PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_include_directories(libtrading PUBLIC ${PROJECT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(libtrading PUBLIC common Threads::Threads)

add_executable(trading_main trading_main.cpp)
target_link_libraries(trading_main PRIVATE libtrading)
target_compile_options(trading_main PRIVATE -Wall -Wextra -Werror -Wpedantic)
