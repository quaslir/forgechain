add_executable(forgechain main.cpp Orchestrator.cpp RpcServer.cpp)

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

add_executable(wallet wallet_main.cpp Wallet.cpp)

target_link_libraries(wallet PRIVATE
    core
    crypto
    consensus
    network
    project_warnings
)

if(ENABLE_ASAN)
    target_link_libraries(wallet PRIVATE project_sanitizers)
endif()
