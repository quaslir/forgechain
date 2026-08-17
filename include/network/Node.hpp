#pragma once

#include "crypto/CommonTypes.hpp"
#include "network/Handshake.hpp"
#include "network/Peer.hpp"
#include "network/TcpSocket.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <utility>
namespace forgechain::network {
    struct PeerEntry {
            std::unique_ptr<Peer> peer;
            std::thread worker;
    };
    using VectorPeers = std::vector<PeerEntry>;
    constexpr std::chrono::milliseconds CLEANER_TIMEOUT = std::chrono::milliseconds(500);
    class Node {
        public:
            Node(uint16_t listen_port, VersionInfo info);
            ~Node();
            bool start();
            void stop();
            bool accept_one_peer();
            void accept_loop();
            void peer_loop(Peer* peer);
            void cleaner_loop();
            bool connect_to_peer(const crypto::str& host, uint16_t port);

            [[nodiscard]] size_t peer_count() const;
        private:
            bool register_new_peer(TcpSocket && socket);
            uint16_t listen_port_;
            VersionInfo info_;
            TcpSocket listener_{-1};
            VectorPeers peers_;
            std::atomic<bool> running_{false};
            std::thread accept_thread_;
            std::thread cleaner_thread_;
            mutable std::mutex peers_mutex_;
    };
}
