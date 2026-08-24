cmake_minimum_required(VERSION 3.16)
project(lineup_optimizer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_library(lineup_core
  src/optimizer.cpp
  src/player.cpp
  src/roster.cpp
  src/csv.cpp
)
target_include_directories(lineup_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
  target_compile_options(lineup_core PRIVATE /W4)
else()
  target_compile_options(lineup_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(lineup src/main.cpp)
target_link_libraries(lineup PRIVATE lineup_core)

add_executable(lineup_bench bench/bench.cpp)
target_link_libraries(lineup_bench PRIVATE lineup_core)

option(LINEUP_BUILD_TESTS "Build the test suite" ON)
if(LINEUP_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(lineup_tests
    tests/test_optimizer.cpp
    tests/test_csv.cpp
    tests/test_player.cpp
  )
  target_link_libraries(lineup_tests PRIVATE lineup_core GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(lineup_tests)
endif()
