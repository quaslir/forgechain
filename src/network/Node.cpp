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
#include <vector>
namespace forgechain::network {
    Node::Node(uint16_t listen_port, VersionInfo info) : listen_port_(listen_port), info_(info) {}

    bool Node::start() {
        listener_ = listen_on(listen_port_);
        if(!listener_.is_valid()) return false;
        listener_.set_receive_timeout(1);
        running_.store(true);
        accept_thread_ = std::thread(&Node::accept_loop, this);
        cleaner_thread_ = std::thread(&Node::cleaner_loop, this);
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
void Node::cleaner_loop() {
    while(running_) {
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for(auto& peer_entry : peers_) {
            if(!peer_entry.peer->is_alive() && peer_entry.worker.joinable()) {
                peer_entry.worker.join();
            }
        }
        std::erase_if(peers_, [](const PeerEntry& peer_entry) {
            return peer_entry.peer->is_alive() == false;
        });
    }

        std::this_thread::sleep_for(CLEANER_TIMEOUT);
    }

}

    bool Node::accept_one_peer() {
       TcpSocket socket  =  accept_connection(listener_);
       if(!socket.is_valid()) return false;
       socket.set_receive_timeout(5);
       auto incoming_info = perform_handshake(socket.fd(), info_);
       if(!incoming_info.has_value()) return false;
       auto peer =std::make_unique<Peer>(std::move(socket), *incoming_info);
       Peer * raw_peer = peer.get();
       std::thread worker{&Node::peer_loop, this, raw_peer};
   {
       std::lock_guard<std::mutex> lock(peers_mutex_);
       peers_.push_back(PeerEntry{.peer = std::move(peer),.worker =  std::move(worker)});
   }

       return true;
    }

    bool Node::connect_to_peer(const crypto::str& host, uint16_t port) {
       TcpSocket socket = connect_to(host, port);
       if(!socket.is_valid()) return false;
       socket.set_receive_timeout(5);
       auto incoming_info = perform_handshake(socket.fd(), info_);
       if(!incoming_info.has_value()) return false;
       auto peer =std::make_unique<Peer>(std::move(socket), *incoming_info);
       Peer * raw_peer = peer.get();
           std::thread worker{&Node::peer_loop, this, raw_peer};
       {
           std::lock_guard<std::mutex> lock(peers_mutex_);
           peers_.push_back(PeerEntry{.peer = std::move(peer),.worker =  std::move(worker)});
       }

       return true;
    }

    [[nodiscard]] size_t Node::peer_count() const {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        return peers_.size();
    }

    void Node::stop() {
        running_.store(false);
        listener_.close_socket();
        std::vector<std::thread> workers_to_join;
        {

        std::lock_guard<std::mutex> lock(peers_mutex_);

        for(auto& peer_entry :peers_) {
            peer_entry.peer->socket().close_socket();
        }

        for(auto& peer_entry :peers_) {
            if(peer_entry.worker.joinable()) {
                workers_to_join.push_back(std::move(peer_entry.worker));
            }
        }
        peers_.clear();
        }

        for(auto& worker : workers_to_join) {
            worker.join();
        }
    }
    Node::~Node() {
        stop();
        if(accept_thread_.joinable()) {
            accept_thread_.join();
        }
        if(cleaner_thread_.joinable()) {
            cleaner_thread_.join();
        }
    }
}
