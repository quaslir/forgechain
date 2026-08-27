#include "network/Handshake.hpp"
#include "network/Message.hpp"
#include "network/Socket.hpp"
#include "network/TcpSocket.hpp"
#include "crypto/CommonTypes.hpp"
#include <gtest/gtest.h>

#include <unistd.h>
#include <optional>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <thread>

using namespace forgechain::network;
using forgechain::crypto::bytes;

namespace {

uint16_t next_test_port() {
    static auto port = static_cast<uint16_t>(31000 + (getpid() % 1000) * 20);
    return port++;
}

}  // namespace

TEST(VersionSerialization, RoundTripPreservesAllFields) {
    VersionInfo original{7, 12345, 1700000000ULL, 0, 0xAABBCCDDULL};

    auto serialized = serialize_version(original);
    VersionInfo restored = deserialize_version(serialized);

    EXPECT_EQ(restored.protocol_version, original.protocol_version);
    EXPECT_EQ(restored.chain_height, original.chain_height);
    EXPECT_EQ(restored.timestamp, original.timestamp);
    EXPECT_EQ(restored.listen_port, original.listen_port);
    EXPECT_EQ(restored.node_id, original.node_id);
}

TEST(VersionSerialization, ProducesExactlyThirtyBytes) {
    VersionInfo info{1, 0, 0, 8000, 42};
    auto serialized = serialize_version(info);
    EXPECT_EQ(serialized.size(), 30u);
}

TEST(VersionSerialization, RoundTripPreservesMaximumValues) {
    VersionInfo edge{0xFFFFFFFF, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
                     0xFFFF, 0xFFFFFFFFFFFFFFFFULL};

    auto serialized = serialize_version(edge);
    VersionInfo restored = deserialize_version(serialized);

    EXPECT_EQ(restored.protocol_version, edge.protocol_version);
    EXPECT_EQ(restored.chain_height, edge.chain_height);
    EXPECT_EQ(restored.timestamp, edge.timestamp);
    EXPECT_EQ(restored.listen_port, edge.listen_port);
    EXPECT_EQ(restored.node_id, edge.node_id);
}

TEST(VersionSerialization, RoundTripPreservesZeroValues) {
    VersionInfo zero{0, 0, 0, 0, 0};

    auto serialized = serialize_version(zero);
    VersionInfo restored = deserialize_version(serialized);

    EXPECT_EQ(restored.protocol_version, 0u);
    EXPECT_EQ(restored.chain_height, 0u);
    EXPECT_EQ(restored.timestamp, 0u);
    EXPECT_EQ(restored.listen_port, 0u);
    EXPECT_EQ(restored.node_id, 0u);
}

TEST(VersionSerialization, FieldsAreInDocumentedOrder) {
    VersionInfo info{1, 42, 1700000000ULL, 9000, 0x1122334455667788ULL};
    auto serialized = serialize_version(info);

    uint32_t first_field;
    std::memcpy(&first_field, serialized.data(), sizeof(first_field));
    EXPECT_EQ(first_field, 1u) << "first 4 bytes must be protocol_version";

    uint64_t second_field;
    std::memcpy(&second_field, serialized.data() + 4, sizeof(second_field));
    EXPECT_EQ(second_field, 42u) << "next 8 bytes must be chain_height";

    uint64_t third_field;
    std::memcpy(&third_field, serialized.data() + 12, sizeof(third_field));
    EXPECT_EQ(third_field, 1700000000ULL) << "next 8 bytes must be timestamp";

    uint16_t fourth_field;
    std::memcpy(&fourth_field, serialized.data() + 20, sizeof(fourth_field));
    EXPECT_EQ(fourth_field, 9000u) << "next 2 bytes must be listen_port";

    uint64_t fifth_field;
    std::memcpy(&fifth_field, serialized.data() + 22, sizeof(fifth_field));
    EXPECT_EQ(fifth_field, 0x1122334455667788ULL)
        << "last 8 bytes must be node_id";
}

TEST(Handshake, BothSidesReceiveEachOthersVersionInfo) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    VersionInfo server_info{1, 100, 1700000000ULL, port, 111};
    VersionInfo client_info{1, 50, 1700000100ULL, 0, 222};

    std::optional<VersionInfo> client_received_from_server;
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        TcpSocket client = connect_to("127.0.0.1", port);
        ASSERT_TRUE(client.is_valid());
        client_received_from_server = perform_handshake(client.fd(), client_info);
    });

    TcpSocket accepted = accept_connection(server);
    ASSERT_TRUE(accepted.is_valid());
    std::optional<VersionInfo> server_received_from_client =
        perform_handshake(accepted.fd(), server_info);

    client_thread.join();

    ASSERT_TRUE(server_received_from_client.has_value());
    EXPECT_EQ(server_received_from_client->protocol_version, client_info.protocol_version);
    EXPECT_EQ(server_received_from_client->chain_height, client_info.chain_height);
    EXPECT_EQ(server_received_from_client->timestamp, client_info.timestamp);
    EXPECT_EQ(server_received_from_client->node_id, client_info.node_id);

    ASSERT_TRUE(client_received_from_server.has_value());
    EXPECT_EQ(client_received_from_server->protocol_version, server_info.protocol_version);
    EXPECT_EQ(client_received_from_server->chain_height, server_info.chain_height);
    EXPECT_EQ(client_received_from_server->timestamp, server_info.timestamp);
    EXPECT_EQ(client_received_from_server->node_id, server_info.node_id);
}

TEST(Handshake, DifferentChainHeightsAreCorrectlyDistinguishable) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    VersionInfo server_info{1, 500, 1700000000ULL, 0, 1};
    VersionInfo client_info{1, 10, 1700000000ULL, 0, 2};

    std::optional<VersionInfo> client_view_of_server;
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        TcpSocket client = connect_to("127.0.0.1", port);
        client_view_of_server = perform_handshake(client.fd(), client_info);
    });

    TcpSocket accepted = accept_connection(server);
    perform_handshake(accepted.fd(), server_info);
    client_thread.join();

    ASSERT_TRUE(client_view_of_server.has_value());
    EXPECT_GT(client_view_of_server->chain_height, client_info.chain_height)
        << "client should be able to see it is behind the server";
}

TEST(Handshake, WrongFirstMessageTypeIsRejected) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        TcpSocket client = connect_to("127.0.0.1", port);
        ASSERT_TRUE(client.is_valid());

        Message discard;
        receive_message(client.fd(), discard);

        Message wrong_type{.type = MessageType::PING, .payload = {}};
        send_message(client.fd(), wrong_type);
    });

    TcpSocket accepted = accept_connection(server);
    VersionInfo server_info{1, 0, 1700000000ULL, 0, 1};
    std::optional<VersionInfo> result = perform_handshake(accepted.fd(), server_info);

    client_thread.join();

    EXPECT_FALSE(result.has_value());
}

TEST(Handshake, TruncatedVersionPayloadIsRejectedNotCrashed) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        TcpSocket client = connect_to("127.0.0.1", port);
        ASSERT_TRUE(client.is_valid());

        Message discard;
        receive_message(client.fd(), discard);

        Message truncated{.type = MessageType::VERSION, .payload = {0x01, 0x02, 0x03}};
        send_message(client.fd(), truncated);
    });

    TcpSocket accepted = accept_connection(server);
    VersionInfo server_info{1, 0, 1700000000ULL, 0, 1};

    std::optional<VersionInfo> result;
    EXPECT_NO_THROW({ result = perform_handshake(accepted.fd(), server_info); });

    client_thread.join();

    EXPECT_FALSE(result.has_value());
}

TEST(Handshake, OversizedVersionPayloadIsRejected) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        TcpSocket client = connect_to("127.0.0.1", port);
        ASSERT_TRUE(client.is_valid());

        Message discard;
        receive_message(client.fd(), discard);

        bytes oversized_payload(31, 0xAA);
        Message oversized{.type = MessageType::VERSION, .payload = oversized_payload};
        send_message(client.fd(), oversized);
    });

    TcpSocket accepted = accept_connection(server);
    VersionInfo server_info{1, 0, 1700000000ULL, 0, 1};
    std::optional<VersionInfo> result = perform_handshake(accepted.fd(), server_info);

    client_thread.join();

    EXPECT_FALSE(result.has_value());
}

TEST(Handshake, ConnectionClosedByPeerBeforeRespondingFailsGracefully) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        TcpSocket client = connect_to("127.0.0.1", port);
        ASSERT_TRUE(client.is_valid());
    });

    TcpSocket accepted = accept_connection(server);
    VersionInfo server_info{1, 0, 1700000000ULL, 0, 1};

    std::optional<VersionInfo> result;
    EXPECT_NO_THROW({ result = perform_handshake(accepted.fd(), server_info); });

    client_thread.join();

    EXPECT_FALSE(result.has_value());
}

TEST(Handshake, EmptyVersionPayloadIsRejected) {
    uint16_t port = next_test_port();
    TcpSocket server = listen_on(port);
    ASSERT_TRUE(server.is_valid());

    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        TcpSocket client = connect_to("127.0.0.1", port);
        ASSERT_TRUE(client.is_valid());

        Message discard;
        receive_message(client.fd(), discard);

        Message empty{.type = MessageType::VERSION, .payload = {}};
        send_message(client.fd(), empty);
    });

    TcpSocket accepted = accept_connection(server);
    VersionInfo server_info{1, 0, 1700000000ULL, 0, 1};
    std::optional<VersionInfo> result = perform_handshake(accepted.fd(), server_info);

    client_thread.join();

    EXPECT_FALSE(result.has_value());
}
