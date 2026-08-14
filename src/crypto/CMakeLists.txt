add_library(crypto
    Hash.cpp
    Keys.cpp
    Signature.cpp
)

target_include_directories(crypto PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(crypto PUBLIC OpenSSL::Crypto project_warnings)
target_compile_options(crypto PRIVATE -Wno-deprecated-declarations)
