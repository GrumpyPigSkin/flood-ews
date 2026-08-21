cmake_minimum_required(VERSION 3.20)
project(raft_tests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)


set(FOG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../fog/src")

add_library(test_common INTERFACE)
target_include_directories(test_common INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${FOG_SRC}"
)
target_compile_options(test_common INTERFACE
  -Wall -Wextra -Wpedantic
)

find_package(Threads REQUIRED)
target_link_libraries(test_common INTERFACE Threads::Threads)
target_include_directories(test_common INTERFACE ../)

# Deterministic tests that are run every time.
set(RAFT_TESTS
  test_asymmetric
  test_catchup
  test_divergence
  test_failover
  test_log_full
  test_restart
  test_snapshot
)

foreach(test_name ${RAFT_TESTS})
  add_executable(${test_name} raft/${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE test_common GTest::gtest_main)
  add_test(NAME ${test_name} COMMAND ${test_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "unit;smoke")
endforeach()

# The soak binary is built once but registered twice, with different roles.
add_executable(test_soak raft/test_soak.cpp)
target_link_libraries(test_soak PRIVATE test_common GTest::gtest)

# Smoke test runs the same test each time for 10s.
add_test(NAME soak_smoke COMMAND test_soak 2909011967 10)
set_tests_properties(soak_smoke PROPERTIES LABELS "smoke" TIMEOUT 60)

# Fuzz test runs a random seed 60s.
add_test(NAME soak_fuzz COMMAND test_soak)
set_tests_properties(soak_fuzz PROPERTIES LABELS "nightly" TIMEOUT 120)

# Outlier vote tests.
add_executable(test_outlier_vote outlier_vote/test_outlier_vote.cpp)
target_link_libraries(test_outlier_vote PRIVATE test_common GTest::gtest_main)
add_test(NAME test_outlier_vote COMMAND test_outlier_vote)
set_tests_properties(test_outlier_vote PROPERTIES LABELS "unit;smoke")

# JSN tests.
add_executable(test_jsn jsn/test_jsn.cpp)
target_link_libraries(test_jsn PRIVATE test_common GTest::gtest_main)
add_test(NAME test_jsn COMMAND test_jsn)
set_tests_properties(test_jsn PROPERTIES LABELS "unit;smoke")

# Replay tests.
add_executable(test_replay security/test_sesnor_replay_detection.cpp)
target_link_libraries(test_replay PRIVATE test_common GTest::gtest_main)
add_test(NAME test_replay COMMAND test_replay)
set_tests_properties(test_replay PROPERTIES LABELS "unit;smoke")
