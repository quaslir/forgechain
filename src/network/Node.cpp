#include "network/Node.hpp"
#include <cstdint>
#include <cstddef>
#include "network/Handshake.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Message.hpp"
#include "network/Peer.hpp"
#include "network/TcpSocket.hpp"
#include <mutex>
#include <thread>
#include <utility>
#include <memory>
namespace forgechain::network {
    Node::Node(uint16_t listen_port, VersionInfo info) : listen_port_(listen_port), info_(info) {}

    bool Node::start() {
        listener_ = listen_on(listen_port_);
        if(!listener_.is_valid()) return false;
        running_.store(true);
        accept_thread_ = std::thread(&Node::accept_loop, this);
        return true;
    }

void Node::accept_loop() {
    while(running_) {
        accept_one_peer();
    }
}
void Node::peer_loop(Peer* peer) {
while(running_ && peer->is_alive()) {
    Message msg;
    if(!receive_message(peer->socket().fd(), msg)) {
        peer->mark_dead();
        break;
    }

    // check msg
}
}

    bool Node::accept_one_peer() {
       TcpSocket socket  =  accept_connection(listener_);
       if(!socket.is_valid()) return false;
       auto incoming_info = perform_handshake(socket.fd(), info_);
       if(!incoming_info.has_value()) return false;
       auto peer =std::make_unique<Peer>(std::move(socket), *incoming_info);
       Peer * raw_peer = peer.get();
       {
           std::lock_guard<std::mutex> lock(peers_mutex_);
           peers_.push_back(std::move(peer));
       }

       std::thread(&Node::peer_loop, this, raw_peer).detach();
       return true;
    }

    bool Node::connect_to_peer(const crypto::str& host, uint16_t port) {
       TcpSocket socket = connect_to(host, port);
       if(!socket.is_valid()) return false;
       auto incoming_info = perform_handshake(socket.fd(), info_);
       if(!incoming_info.has_value()) return false;
       auto peer =std::make_unique<Peer>(std::move(socket), *incoming_info);
       Peer * raw_peer = peer.get();
       {
           std::lock_guard<std::mutex> lock(peers_mutex_);
           peers_.push_back(std::move(peer));
       }

       std::thread(&Node::peer_loop, this, raw_peer).detach();
       return true;
    }

    [[nodiscard]] size_t Node::peer_count() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        return peers_.size();
    }

    void Node::stop() {
        running_.store(false);
        listener_.close_socket();
    }
    Node::~Node() {
        stop();
        if(accept_thread_.joinable()) {
            accept_thread_.join();
        }
    }
}
