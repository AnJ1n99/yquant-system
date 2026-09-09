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
