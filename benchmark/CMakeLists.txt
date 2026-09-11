include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

find_package(Threads REQUIRED)

add_executable(fast_queue_test FastQueue.cc)
target_link_libraries(fast_queue_test PRIVATE GTest::gtest Threads::Threads)

# 撮合回归测试复用已有的 GoogleTest 依赖。
add_executable(book_core_test BookCore.cc)
target_link_libraries(book_core_test PRIVATE GTest::gtest_main libexchange common)
