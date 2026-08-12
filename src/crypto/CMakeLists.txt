add_library(crypto
    Hash.cpp
)

target_include_directories(crypto PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(crypto PUBLIC OpenSSL::Crypto project_warnings)
