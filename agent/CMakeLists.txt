add_executable(agent
  src/agent_main.cpp
)
target_link_libraries(agent PRIVATE common Psapi)
