#include "network/TcpSocket.hpp"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <cstdint>
#include <utility>
using namespace forgechain::network;

namespace {

uint16_t next_test_port() {
    static uint16_t port = 30000;
    return port++;
}

bool fd_is_open(int fd) {
    return fcntl(fd, F_GETFD) != -1;
}

}  // namespace

TEST(TcpSocket, ListenOnValidPortSucceeds) {
    TcpSocket server = listen_on(next_test_port());
    EXPECT_TRUE(server.is_valid());
    EXPECT_GE(server.fd(), 0);
}

TEST(TcpSocket, DefaultConstructedFromNegativeFdIsInvalid) {
    TcpSocket socket(-1);
    EXPECT_FALSE(socket.is_valid());
}

TEST(TcpSocket, DestructorClosesTheFileDescriptor) {
    int raw_fd;
    {
        TcpSocket server = listen_on(next_test_port());
        ASSERT_TRUE(server.is_valid());
        raw_fd = server.fd();
        EXPECT_TRUE(fd_is_open(raw_fd));
    }

    EXPECT_FALSE(fd_is_open(raw_fd));
}

TEST(TcpSocket, DestructingAnInvalidSocketDoesNotCrashOrCloseUnrelatedFds) {
    ASSERT_TRUE(fd_is_open(0)) << "stdin should be open at test start";
    {
        TcpSocket invalid(-1);
    }
    EXPECT_TRUE(fd_is_open(0)) << "stdin must not be closed by destroying an invalid TcpSocket";
}

TEST(TcpSocket, MoveConstructorTransfersFd) {
    TcpSocket original = listen_on(next_test_port());
    ASSERT_TRUE(original.is_valid());
    int original_fd = original.fd();

    TcpSocket moved(std::move(original));

    EXPECT_EQ(moved.fd(), original_fd);
    EXPECT_TRUE(moved.is_valid());
}

TEST(TcpSocket, MoveConstructorLeavesSourceInvalid) {
    TcpSocket original = listen_on(next_test_port());
    ASSERT_TRUE(original.is_valid());

    TcpSocket moved(std::move(original));

    EXPECT_FALSE(original.is_valid());
}

TEST(TcpSocket, MoveConstructorDoesNotLeaveSourcePointingAtStdin) {
    TcpSocket original = listen_on(next_test_port());
    TcpSocket moved(std::move(original));

    EXPECT_NE(original.fd(), 0);
}

TEST(TcpSocket, MovedFromObjectDestructorDoesNotDoubleClose) {
    int transferred_fd;
    {
        TcpSocket destination = listen_on(next_test_port());
        {
            TcpSocket source = listen_on(next_test_port());
            transferred_fd = source.fd();
            destination = std::move(source);
        }
        EXPECT_TRUE(fd_is_open(transferred_fd))
            << "fd was closed prematurely by the moved-from source's destructor";
    }
    EXPECT_FALSE(fd_is_open(transferred_fd));
}

TEST(TcpSocket, MoveAssignmentClosesPreviouslyOwnedFd) {
    TcpSocket a = listen_on(next_test_port());
    ASSERT_TRUE(a.is_valid());
    int a_original_fd = a.fd();
    EXPECT_TRUE(fd_is_open(a_original_fd));

    TcpSocket b = listen_on(next_test_port());
    ASSERT_TRUE(b.is_valid());

    a = std::move(b);

    EXPECT_FALSE(fd_is_open(a_original_fd))
        << "move-assignment leaked the previously-owned fd instead of closing it";
}

TEST(TcpSocket, MoveAssignmentTransfersFdAndInvalidatesSource) {
    TcpSocket a = listen_on(next_test_port());
    TcpSocket b = listen_on(next_test_port());
    ASSERT_TRUE(a.is_valid());
    ASSERT_TRUE(b.is_valid());
    int b_fd = b.fd();

    a = std::move(b);

    EXPECT_EQ(a.fd(), b_fd);
    EXPECT_TRUE(a.is_valid());
    EXPECT_FALSE(b.is_valid());
}

TEST(TcpSocket, SelfMoveAssignmentDoesNotCrashOrCloseTheSocket) {
    TcpSocket a = listen_on(next_test_port());
    ASSERT_TRUE(a.is_valid());
    int original_fd = a.fd();

    TcpSocket& self_ref = a;
    a = std::move(self_ref);

    EXPECT_TRUE(a.is_valid());
    EXPECT_EQ(a.fd(), original_fd);
    EXPECT_TRUE(fd_is_open(original_fd));
}

TEST(TcpSocket, ListenConnectAcceptEstablishesAWorkingConnection) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    TcpSocket client_side{-1};
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client_side = connect_to("127.0.0.1", port);
    });

    TcpSocket server_side = accept_connection(server);
    client_thread.join();

    EXPECT_TRUE(server_side.is_valid());
    EXPECT_TRUE(client_side.is_valid());
    EXPECT_NE(server_side.fd(), client_side.fd());
}

TEST(TcpSocket, DataSentByClientIsReceivedByServer) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    TcpSocket client_side{-1};
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client_side = connect_to("127.0.0.1", port);
        const char* msg = "hello";
        ASSERT_NE(write(client_side.fd(), msg, 5), -1);
    });

    TcpSocket server_side = accept_connection(server);
    client_thread.join();

    char buffer[6] = {0};
    ssize_t n = read(server_side.fd(), buffer, 5);

    EXPECT_EQ(n, 5);
    EXPECT_STREQ(buffer, "hello");
}

TEST(TcpSocket, DataFlowsInBothDirections) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    TcpSocket client_side{-1};
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client_side = connect_to("127.0.0.1", port);
        write(client_side.fd(), "ping", 4);

        char reply[5] = {0};
        read(client_side.fd(), reply, 4);
        EXPECT_STREQ(reply, "pong");
    });

    TcpSocket server_side = accept_connection(server);

    char request[5] = {0};
    read(server_side.fd(), request, 4);
    EXPECT_STREQ(request, "ping");
    write(server_side.fd(), "pong", 4);

    client_thread.join();
}

TEST(TcpSocket, MultipleSequentialConnectionsToSameListenerWork) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    for (int i = 0; i < 3; ++i) {
        TcpSocket client_side{-1};
        std::thread client_thread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            client_side = connect_to("127.0.0.1", port);
        });

        TcpSocket server_side = accept_connection(server);
        client_thread.join();

        EXPECT_TRUE(server_side.is_valid()) << "failed on connection " << i;
        EXPECT_TRUE(client_side.is_valid()) << "failed on connection " << i;
    }
}

TEST(TcpSocket, ConnectToClosedPortFailsGracefully) {
    uint16_t port = next_test_port();
    TcpSocket result = connect_to("127.0.0.1", port);

    EXPECT_FALSE(result.is_valid());
}

TEST(TcpSocket, ConnectToInvalidAddressFailsGracefully) {
    uint16_t port = next_test_port();
    TcpSocket result = connect_to("not-a-valid-ip-address", port);

    EXPECT_FALSE(result.is_valid());
}

TEST(TcpSocket, ListeningTwiceOnTheSamePortFails) {
    uint16_t port = next_test_port();
    TcpSocket first = listen_on(port);
    ASSERT_TRUE(first.is_valid());

    TcpSocket second = listen_on(port);
    EXPECT_FALSE(second.is_valid());
}
