#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace royale::net {

using PeerId = uint32_t;

struct NetEvent {
    enum class Type : uint8_t { Connected, Disconnected, Data } type = Type::Data;
    PeerId peer = 0;
    std::vector<uint8_t> data; // Data only
};

// The server and the client talk to the network only through this interface, so the same game code runs over real UDP
// (ENetTransport), in memory with simulated latency and loss (LoopbackNetwork, used by tests), or anything else.
//
// A server-side transport sees many peers (one per connected client). A client-side transport has exactly one peer,
// the server, and its id is always 0.
class Transport {
  public:
    virtual ~Transport() = default;
    // Next pending event, or false if none.
    virtual bool Poll(NetEvent& out) = 0;
    // `reliable` messages arrive in order and exactly once. Unreliable ones may be lost or dropped if stale.
    virtual void Send(PeerId peer, const uint8_t* data, size_t size, bool reliable) = 0;
    virtual void Disconnect(PeerId peer) = 0;
    // Push queued packets to the wire. Called once per tick.
    virtual void Flush() {}

    void Send(PeerId peer, const std::vector<uint8_t>& data, bool reliable) { Send(peer, data.data(), data.size(), reliable); }
};

} // namespace royale::net
