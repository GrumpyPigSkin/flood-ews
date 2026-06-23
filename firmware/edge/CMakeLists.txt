# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

include(FetchContent)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(edge)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_TAG        e69e5f977d458f2650bb346dadf2ad30c5320281) # 10.2.1
FetchContent_MakeAvailable(fmt)

target_include_directories(app PRIVATE src)
target_link_libraries(app PRIVATE fmt::fmt)

target_sources(app PRIVATE
    src/main.cpp
)

include(${ZEPHYR_BASE}/samples/net/common/common.cmake)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)
