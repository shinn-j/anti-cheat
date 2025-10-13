cmake_minimum_required(VERSION 3.25)
project(anti_cheat LANGUAGES C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(common)
add_subdirectory(game_harness)
add_subdirectory(agent)