add_executable(agent
  src/agent_main.cpp
  src/win_agent.cpp
)

# Belt + suspenders: add the include dir explicitly for agent
target_include_directories(agent PRIVATE
  ${CMAKE_SOURCE_DIR}/common/include
)

# Link 'common' so PUBLIC includes also propagate
target_link_libraries(agent PRIVATE common)

if (WIN32 AND ENABLE_WINDOWS_FEATURES)
  target_link_libraries(agent PRIVATE Psapi)
  target_compile_definitions(agent PRIVATE ANTI_CHEAT_WINDOWS=1)
endif()
