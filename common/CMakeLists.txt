# common/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

add_library(common
  src/log.cpp
)

# Expose headers in common/include to anyone linking 'common'
target_include_directories(common
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
