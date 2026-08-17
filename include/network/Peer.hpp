#pragma once

#include "network/Handshake.hpp"
#include "network/TcpSocket.hpp"
#include <atomic>
namespace forgechain::network {
    class Peer {
        public:
            Peer(TcpSocket socket, VersionInfo remote_version);
            Peer(const Peer&) = delete;
            Peer & operator=(const Peer&) = delete;
            Peer(Peer &&) noexcept;
            Peer& operator=(Peer&&) noexcept;

            TcpSocket& socket();
           [[nodiscard]] const VersionInfo& remote_version() const;
           [[nodiscard]] bool is_alive() const;
           void mark_dead();

        private:
            TcpSocket socket_;
            VersionInfo remote_version_;
            std::atomic<bool> alive_{true};
            };
}
