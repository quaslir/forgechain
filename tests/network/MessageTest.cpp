#include "network/Message.hpp"
#include "network/Socket.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <cstdint>
#include <stdexcept>
#include <vector>
using namespace forgechain::network;

namespace {

struct SocketPair {
    int fds[2];

    SocketPair() {
        int result = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        if (result != 0) {
            throw std::runtime_error("socketpair() failed");
        }
    }

    ~SocketPair() {
        close(fds[0]);
        close(fds[1]);
    }

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;
};

bool send_on_thread(int fd, const Message& msg, std::thread& senderThread) {
    senderThread = std::thread([fd, msg]() {
        send_message(fd, msg);
    });
    return true;
}

}  // namespace

TEST(MessageRoundTrip, BasicPayloadRoundTrips) {
    SocketPair pair;

    Message sent;
    sent.type = MessageType::PING;
    sent.payload = {1, 2, 3, 4, 5, 6, 7, 8};

    std::thread sender;
    send_on_thread(pair.fds[0], sent, sender);

    Message received;
    ASSERT_TRUE(receive_message(pair.fds[1], received));
    sender.join();

    EXPECT_EQ(received.type, sent.type);
    EXPECT_EQ(received.payload, sent.payload);
}

TEST(MessageRoundTrip, EmptyPayloadRoundTrips) {
    SocketPair pair;

    Message sent;
    sent.type = MessageType::PONG;
    sent.payload = {};

    std::thread sender;
    send_on_thread(pair.fds[0], sent, sender);

    Message received;
    ASSERT_TRUE(receive_message(pair.fds[1], received));
    sender.join();

    EXPECT_EQ(received.type, MessageType::PONG);
    EXPECT_TRUE(received.payload.empty());
}

TEST(MessageRoundTrip, EveryMessageTypeRoundTrips) {
    std::vector<MessageType> allTypes = {
        MessageType::VERSION, MessageType::INV,       MessageType::GETDATA,
        MessageType::BLOCK,   MessageType::TX,         MessageType::GETBLOCKS,
        MessageType::PING,    MessageType::PONG,
    };

    for (auto type : allTypes) {
        SocketPair pair;

        Message sent;
        sent.type = type;
        sent.payload = {0xAA, 0xBB};

        std::thread sender;
        send_on_thread(pair.fds[0], sent, sender);

        Message received;
        ASSERT_TRUE(receive_message(pair.fds[1], received))
            << "failed for type " << static_cast<int>(type);
        sender.join();

        EXPECT_EQ(received.type, type);
    }
}

TEST(MessageRoundTrip, LargePayloadAcrossMultiplePartialReadsWrites) {
    SocketPair pair;

    Message sent;
    sent.type = MessageType::TX;
    sent.payload = forgechain::crypto::bytes(500'000, 0x42);

    std::thread sender;
    send_on_thread(pair.fds[0], sent, sender);

    Message received;
    ASSERT_TRUE(receive_message(pair.fds[1], received));
    sender.join();

    ASSERT_EQ(received.payload.size(), sent.payload.size());
    EXPECT_EQ(received.payload, sent.payload);
}

TEST(MessageRoundTrip, PayloadContentIsExactNotJustLength) {
    SocketPair pair;

    Message sent;
    sent.type = MessageType::BLOCK;
    for (int i = 0; i < 256; ++i) {
        sent.payload.push_back(static_cast<uint8_t>(i));
    }

    std::thread sender;
    send_on_thread(pair.fds[0], sent, sender);

    Message received;
    ASSERT_TRUE(receive_message(pair.fds[1], received));
    sender.join();

    ASSERT_EQ(received.payload.size(), 256u);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(received.payload[static_cast<size_t>(i)], static_cast<uint8_t>(i))
            << "byte mismatch at index " << i;
    }
}

TEST(MessageRoundTrip, MultipleMessagesInSequenceDoNotBleedIntoEachOther) {
    SocketPair pair;

    Message first;
    first.type = MessageType::PING;
    first.payload = {0x11, 0x11};

    Message second;
    second.type = MessageType::PONG;
    second.payload = {0x22, 0x22, 0x22};

    std::thread sender([&]() {
        send_message(pair.fds[0], first);
        send_message(pair.fds[0], second);
    });

    Message receivedFirst;
    Message receivedSecond;
    ASSERT_TRUE(receive_message(pair.fds[1], receivedFirst));
    ASSERT_TRUE(receive_message(pair.fds[1], receivedSecond));
    sender.join();

    EXPECT_EQ(receivedFirst.type, MessageType::PING);
    EXPECT_EQ(receivedFirst.payload, first.payload);
    EXPECT_EQ(receivedSecond.type, MessageType::PONG);
    EXPECT_EQ(receivedSecond.payload, second.payload);
}

TEST(MessageValidation, RejectsWrongMagicBytes) {
    SocketPair pair;

    uint8_t badHeader[HEADER_LENGTH] = {'X', 'X', 'X', 'X', 0x00, 0, 0, 0, 0};
    ASSERT_TRUE(send_exact(pair.fds[0], badHeader, sizeof(badHeader)));

    Message received;
    EXPECT_FALSE(receive_message(pair.fds[1], received));
}

TEST(MessageValidation, RejectsPartiallyCorrectMagic) {
    SocketPair pair;

    uint8_t badHeader[HEADER_LENGTH] = {'F', 'R', 'G', 'X', 0x00, 0, 0, 0, 0};
    ASSERT_TRUE(send_exact(pair.fds[0], badHeader, sizeof(badHeader)));

    Message received;
    EXPECT_FALSE(receive_message(pair.fds[1], received));
}

TEST(MessageValidation, RejectsPayloadLengthExceedingMaximum) {
    SocketPair pair;

    uint8_t header[HEADER_LENGTH];
    header[0] = 'F';
    header[1] = 'R';
    header[2] = 'G';
    header[3] = 'C';
    header[4] = static_cast<uint8_t>(MessageType::VERSION);
    uint32_t oversized = MAX_PAYLOAD_SIZE + 1;
    std::memcpy(header + 5, &oversized, sizeof(oversized));

    ASSERT_TRUE(send_exact(pair.fds[0], header, sizeof(header)));

    Message received;
    EXPECT_FALSE(receive_message(pair.fds[1], received));
}

TEST(MessageValidation, AcceptsPayloadLengthExactlyAtMaximum) {
    SocketPair pair;

    uint8_t header[HEADER_LENGTH];
    header[0] = 'F';
    header[1] = 'R';
    header[2] = 'G';
    header[3] = 'C';
    header[4] = static_cast<uint8_t>(MessageType::VERSION);
    uint32_t atMax = MAX_PAYLOAD_SIZE;
    std::memcpy(header + 5, &atMax, sizeof(atMax));

    ASSERT_TRUE(send_exact(pair.fds[0], header, sizeof(header)));
    close(pair.fds[0]);

    Message received;
    EXPECT_FALSE(receive_message(pair.fds[1], received));
}

TEST(MessageValidation, ConnectionClosedBeforeHeaderCompleteFails) {
    SocketPair pair;

    uint8_t partialHeader[4] = {'F', 'R', 'G', 'C'};
    ASSERT_TRUE(send_exact(pair.fds[0], partialHeader, sizeof(partialHeader)));
    close(pair.fds[0]);

    Message received;
    EXPECT_FALSE(receive_message(pair.fds[1], received));
}

TEST(MessageValidation, ConnectionClosedDuringPayloadFails) {
    SocketPair pair;

    uint8_t header[HEADER_LENGTH];
    header[0] = 'F';
    header[1] = 'R';
    header[2] = 'G';
    header[3] = 'C';
    header[4] = static_cast<uint8_t>(MessageType::TX);
    uint32_t announcedLength = 100;
    std::memcpy(header + 5, &announcedLength, sizeof(announcedLength));

    ASSERT_TRUE(send_exact(pair.fds[0], header, sizeof(header)));

    uint8_t partialPayload[10] = {0};
    ASSERT_TRUE(send_exact(pair.fds[0], partialPayload, sizeof(partialPayload)));
    close(pair.fds[0]);

    Message received;
    EXPECT_FALSE(receive_message(pair.fds[1], received));
}

TEST(MessageType, EnumValuesMatchDocumentedProtocolCodes) {
    EXPECT_EQ(static_cast<uint8_t>(MessageType::VERSION), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::INV), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::GETDATA), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::BLOCK), 0x03);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::TX), 0x04);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::GETBLOCKS), 0x05);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::PING), 0x06);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::PONG), 0x07);
}

TEST(MessageType, MagicBytesMatchDocumentedValue) {
    EXPECT_EQ(MAGIC_BYTES[0], 'F');
    EXPECT_EQ(MAGIC_BYTES[1], 'R');
    EXPECT_EQ(MAGIC_BYTES[2], 'G');
    EXPECT_EQ(MAGIC_BYTES[3], 'C');
}

TEST(MessageType, HeaderLengthMatchesFieldSizes) {
    EXPECT_EQ(HEADER_LENGTH, 9u);
}
