add_executable(game_harness
  src/game_main.cpp
)
target_link_libraries(game_harness PRIVATE common Ws2_32)
