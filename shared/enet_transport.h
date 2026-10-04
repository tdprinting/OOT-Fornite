#pragma once
#include "transport.h"
#include <deque>
#include <map>
#include <memory>
#include <string>

struct _ENetHost;
struct _ENetPeer;

namespace royale::net {

constexpr uint16_t kDefaultPort = 7777;

// Real UDP transport on top of ENet (reliable and sequenced-unreliable channels, fragmentation, connection handshake,
// timeouts). Plain C sockets underneath, so the same code runs on Windows (Winsock) and Android (NDK).
//
// Host() listens for clients, Connect() joins a host. A host's own player joins through 127.0.0.1 like everyone else.
class ENetTransport : public Transport {
  public:
    // Listen on `port` (0 = pick a free one, see Port()). `bindAddress` null means all interfaces.
    static std::unique_ptr<ENetTransport> Host(uint16_t port, size_t maxClients, std::string* error = nullptr);
    // Start connecting to a host. The result of the attempt arrives as a Connected or Disconnected event.
    static std::unique_ptr<ENetTransport> Connect(const std::string& host, uint16_t port, std::string* error = nullptr);

    ~ENetTransport() override;
    ENetTransport(const ENetTransport&) = delete;
    ENetTransport& operator=(const ENetTransport&) = delete;

    bool Poll(NetEvent& out) override;
    void Send(PeerId peer, const uint8_t* data, size_t size, bool reliable) override;
    void Disconnect(PeerId peer) override;
    void Flush() override;

    uint16_t Port() const { return port; }

  private:
    ENetTransport() = default;
    void Pump();

    _ENetHost* host = nullptr;
    bool isServer = false;
    uint16_t port = 0;
    PeerId nextPeer = 1;
    std::map<PeerId, _ENetPeer*> peers;
    std::deque<NetEvent> queue;
};

} // namespace royale::net
