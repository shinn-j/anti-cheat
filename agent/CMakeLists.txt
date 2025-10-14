# agent/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

add_executable(agent
  src/agent_main.cpp
  src/model_loader.cpp
  # src/win_agent.cpp      # keep this uncommented only on Windows
)

# agent's private includes (for agent/include)
target_include_directories(agent
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link with common (this pulls in common/include as PUBLIC)
target_link_libraries(agent
  PRIVATE
    common
)
