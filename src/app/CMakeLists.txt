add_executable(forgechain main.cpp)

target_link_libraries(forgechain PRIVATE
    core
    crypto
    project_warnings
)

if(ENABLE_ASAN)
    target_link_libraries(forgechain PRIVATE project_sanitizers)
endif()
