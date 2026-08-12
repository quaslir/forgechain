add_executable(forgechain main.cpp)

if(ENABLE_ASAN)
    target_link_libraries(forgechain PRIVATE project_sanitizers)
endif()
