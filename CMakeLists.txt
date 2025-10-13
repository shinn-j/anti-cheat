cmake_minimum_required(VERSION 3.25)
project(anti_cheat LANGUAGES C CXX)

option(ENABLE_WINDOWS_FEATURES "Build Windows-only anti-cheat features" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_options(/W4 /WX /permissive- /sdl)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(common)
add_subdirectory(game_harness)
add_subdirectory(agent)
