add_library(network
Socket.cpp
Message.cpp
TcpSocket.cpp
Handshake.cpp
)

target_include_directories(network PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(network PUBLIC project_warnings)
