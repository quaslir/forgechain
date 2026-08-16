#pragma once

#include "crypto/CommonTypes.hpp"
#include "network/Handshake.hpp"
#include "network/Peer.hpp"
#include "network/TcpSocket.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace forgechain::network {
    class Node {
        public:
            Node(uint16_t listen_port, VersionInfo info);

            bool start();

            bool accept_one_peer();

            bool connect_to_peer(const crypto::str& host, uint16_t port);

            [[nodiscard]] size_t peer_count() const;
        private:

            uint16_t listen_port_;
            VersionInfo info_;
            TcpSocket listener_{-1};
            std::vector<Peer> peers_;
    };
}
