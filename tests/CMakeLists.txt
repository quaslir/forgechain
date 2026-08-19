include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)


function(add_forgechain_test_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE
        crypto
        core
        consensus
        network
        GTest::gtest_main
        project_warnings
    )
    gtest_discover_tests(${name})
endfunction()

add_forgechain_test_executable(crypto_tests
    crypto/HashTest.cpp
    crypto/KeysTest.cpp
    crypto/SignatureTest.cpp
)

add_forgechain_test_executable(core_tests
    core/BlockTest.cpp
    core/BlockchainTest.cpp
    core/LedgerTest.cpp
    core/MempoolTest.cpp
    core/OrphanPoolTest.cpp
)

add_forgechain_test_executable(consensus_tests
    consensus/ProofOfWorkTest.cpp
    consensus/RetargetTest.cpp
)

add_forgechain_test_executable(integration_tests
    integration/FullBlockchainLifecycleTest.cpp
)
add_forgechain_test_executable(network_tests
    network/MessageTest.cpp
    network/TcpSocketTest.cpp
    network/HandshakeTest.cpp
    network/InventoryTest.cpp
    network/PropagationTest.cpp
    network/NodeTest.cpp
)
