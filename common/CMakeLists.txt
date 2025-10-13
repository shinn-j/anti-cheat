add_library(common STATIC
  src/log.cpp
)

# Export the public include dir to dependents
target_include_directories(common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
