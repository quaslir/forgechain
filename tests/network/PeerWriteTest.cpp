// Several threads write to one peer socket: peer_loop answering GETBLOCKS,
// ping_loop sending PING, broadcast_inv relaying from other peers. Without a
// write lock their frames interleave and the stream is unparseable from the
// first corrupted header on.

#include "network/Message.hpp"
#include "network/Peer.hpp"
#include "network/TcpSocket.hpp"
#include "network/Socket.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>
#include <mutex>
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <utility>
#include <unistd.h>
using namespace forgechain::network;
using forgechain::crypto::bytes;

namespace {
    uint16_t next_test_port() {
      static auto port = static_cast<uint16_t>(31000 + (getpid() % 1000) * 20);
      return port++;
    }
constexpr int kThreads = 6;
constexpr int kMessagesPerThread = 60;

bytes payload_of(size_t size, uint8_t fill) { return bytes(size, fill); }

} // namespace

TEST(PeerWrite, ConcurrentSendsProduceIntactFrames) {
  uint16_t port = next_test_port();
  TcpSocket listener = listen_on(port);
  ASSERT_TRUE(listener.is_valid());

  std::atomic<int> received{0};
  std::atomic<bool> corrupt{false};
  std::thread reader([&] {
    TcpSocket server = accept_connection(listener);
    if (!server.is_valid())
      return;
    Message msg;
    while (receive_message(server.fd(), msg)) {
      if (!msg.payload.empty()) {
        uint8_t fill = msg.payload[0];
        size_t expected = static_cast<size_t>(fill) * 100;
        if (msg.payload.size() != expected)
          corrupt = true;
        for (uint8_t b : msg.payload) {
          if (b != fill)
            corrupt = true;
        }
      }
      if (++received == kThreads * kMessagesPerThread)
        break;
    }
  });

  TcpSocket client = connect_to("127.0.0.1", port);
  ASSERT_TRUE(client.is_valid());
  Peer peer(std::move(client), VersionInfo{}, "127.0.0.1");

  std::vector<std::thread> writers;
  for (int t = 0; t < kThreads; t++) {
    writers.emplace_back([&peer, t] {
      auto fill = static_cast<uint8_t>(t + 1);
      Message msg{.type = MessageType::BLOCK,
                  .payload = payload_of(static_cast<size_t>(fill) * 100, fill)};
      for (int i = 0; i < kMessagesPerThread; i++) {
        std::lock_guard<std::mutex> lock(peer.write_mutex());
        send_message(peer.socket().fd(), msg);
      }
    });
  }
  for (auto &w : writers)
    w.join();
  reader.join();

  EXPECT_FALSE(corrupt) << "frames interleaved on the wire";
  EXPECT_EQ(received, kThreads * kMessagesPerThread);
}
