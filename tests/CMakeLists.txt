include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP true
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(forgechain_tests
    crypto/HashTest.cpp
    core/BlockTest.cpp
    core/BlockchainTest.cpp
     consensus/ProofOfWorkTest.cpp
)

target_link_libraries(forgechain_tests PRIVATE
crypto
core
consensus
    GTest::gtest_main
    project_warnings
)

include(GoogleTest)
gtest_discover_tests(forgechain_tests)
