add_executable(game_harness src/game_main.cpp)
target_link_libraries(game_harness PRIVATE common)
if (WIN32)
  target_link_libraries(game_harness PRIVATE Ws2_32) # future UDP
endif()
