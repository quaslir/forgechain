#pragma once

#include "network/Handshake.hpp"
#include "network/TcpSocket.hpp"
namespace forgechain::network {
    class Peer {
        public:
            Peer(TcpSocket socket, VersionInfo remote_version);
            Peer(const Peer&) = delete;
            Peer & operator=(const Peer&) = delete;
            Peer(Peer &&) = default;
            Peer& operator=(Peer&&) = default;

            TcpSocket& socket();
           [[nodiscard]] const VersionInfo& remote_version() const;

        private:
            TcpSocket socket_;
            VersionInfo remote_version_;
            };
}
