add_executable(forgechain main.cpp Orchestrator.cpp)

target_link_libraries(forgechain PRIVATE
    core
    crypto
    consensus
    network
    project_warnings
)

if(ENABLE_ASAN)
    target_link_libraries(forgechain PRIVATE project_sanitizers)
endif()

add_executable(peer_a peer_node_demo.cpp)
add_executable(peer_b peer_node_demo.cpp)

foreach(demo_target peer_a peer_b)
    target_link_libraries(${demo_target} PRIVATE
        core
        crypto
        consensus
        network
        project_warnings
    )
    if(ENABLE_ASAN)
        target_link_libraries(${demo_target} PRIVATE project_sanitizers)
    endif()
endforeach()
