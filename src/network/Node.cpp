#include "network/Node.hpp"
#include <cstdint>
#include <cstddef>
#include "network/Handshake.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Peer.hpp"
#include "network/TcpSocket.hpp"
#include <utility>
namespace forgechain::network {
    Node::Node(uint16_t listen_port, VersionInfo info) : listen_port_(listen_port), info_(info), listener_(-1) {}

    bool Node::start() {
        listener_ = listen_on(listen_port_);
        if(!listener_.is_valid()) return false;
        return true;
    }

    bool Node::accept_one_peer() {
       TcpSocket socket  =  accept_connection(listener_);
       if(!socket.is_valid()) return false;
       auto incoming_info = perform_handshake(socket.fd(), info_);
       if(!incoming_info.has_value()) return false;
       Peer peer{std::move(socket), *incoming_info};
       peers_.push_back(std::move(peer));
       return true;
    }

    bool Node::connect_to_peer(const crypto::str& host, uint16_t port) {
       TcpSocket socket = connect_to(host, port);
       if(!socket.is_valid()) return false;
       auto incoming_info = perform_handshake(socket.fd(), info_);
       if(!incoming_info.has_value()) return false;
       Peer peer{std::move(socket), *incoming_info};
       peers_.push_back(std::move(peer));
       return true;
    }

    [[nodiscard]] size_t Node::peer_count() const {
        return peers_.size();
    }
}
