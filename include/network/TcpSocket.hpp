#pragma once

#include "crypto/CommonTypes.hpp"
#include <cstdint>
namespace forgechain::network {
    class TcpSocket {
        public:
            explicit TcpSocket(int fd);
            ~TcpSocket();

            TcpSocket(const TcpSocket&) = delete;
            TcpSocket& operator=(const TcpSocket&) = delete;
            TcpSocket(TcpSocket && other) noexcept;
            TcpSocket& operator=(TcpSocket&& other) noexcept;

          [[nodiscard]]  int fd() const;
          [[nodiscard]] bool is_valid() const;

        private:
            int fd_;
    };

    TcpSocket listen_on(uint16_t port);
    TcpSocket accept_connection(const TcpSocket& listener);
    TcpSocket connect_to(const crypto::str& host, uint16_t port);
}
