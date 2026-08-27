add_library(network
Socket.cpp
Message.cpp
TcpSocket.cpp
Handshake.cpp
Peer.cpp
Node.cpp
Inventory.cpp
GetBlocks.cpp
Heartbeat.cpp
Bootstrap.cpp
PeerAddress.cpp
NodeId.cpp
)

target_include_directories(network PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(network PUBLIC crypto project_warnings)
