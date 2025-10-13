add_library(common STATIC
  src/log.cpp
)

# export headers in common/include to anything linking common
target_include_directories(common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
