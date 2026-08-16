#include "network/Peer.hpp"
#include "network/Handshake.hpp"
#include "network/TcpSocket.hpp"
#include <utility>
namespace forgechain::network {
    Peer::Peer(TcpSocket socket, VersionInfo remote_version) : socket_(std::move(socket)), remote_version_(remote_version) {}
   const VersionInfo& Peer::remote_version() const {
       return remote_version_;
   }
   TcpSocket& Peer::socket() {
return socket_;
   }
}
