#pragma once
#include "rng.h"
#include "transport.h"
#include <algorithm>
#include <deque>
#include <memory>

namespace royale::net {

// In-memory network for tests and tools: one server endpoint, any number of client endpoints, with simulated one-way
// latency, jitter and unreliable-packet loss on a virtual clock (Advance). Delivery on each link stays in order (like
// ENet's sequenced channels), so "unreliable" means "may be lost", never "reordered".
class LoopbackNetwork {
  public:
    struct Link {
        float latencySec = 0;
        float jitterSec = 0;
        float unreliableLoss = 0; // 0..1
    };

    explicit LoopbackNetwork(uint64_t seed = 1) : rng(seed), server(*this, true, 0) {}

    Transport& Server() { return server; }

    // Create a client and connect it. Both ends get a Connected event once the latency has elapsed.
    Transport& NewClient() { return NewClient(Link()); }
    Transport& NewClient(Link link) {
        PeerId id = nextPeer++;
        clients.push_back(std::make_unique<Endpoint>(*this, false, id));
        Endpoint& c = *clients.back();
        c.link = link;
        Push(c, id, NetEvent::Type::Connected, {}, false, true);
        Push(server, id, NetEvent::Type::Connected, {}, false, false, &c);
        return c;
    }

    void Advance(float dt) { now += dt; }
    float Now() const { return now; }

    uint64_t bytesSentByServer = 0;
    uint64_t packetsSentByServer = 0;

  private:
    struct Pending {
        float at;
        NetEvent ev;
    };

    class Endpoint : public Transport {
      public:
        Endpoint(LoopbackNetwork& n, bool isServer, PeerId id) : net(n), isServer(isServer), selfId(id) {}
        bool Poll(NetEvent& out) override {
            if (inbox.empty() || inbox.front().at > net.now) return false;
            out = std::move(inbox.front().ev);
            inbox.pop_front();
            return true;
        }
        void Send(PeerId peer, const uint8_t* data, size_t size, bool reliable) override {
            net.Deliver(*this, peer, data, size, reliable);
        }
        void Disconnect(PeerId peer) override { net.Drop(*this, peer); }

        LoopbackNetwork& net;
        bool isServer;
        PeerId selfId;
        Link link;
        bool open = true;
        std::deque<Pending> inbox;
        float lastAt = 0; // last scheduled delivery time into this inbox, keeps ordering
    };

    Endpoint* ClientFor(PeerId id) {
        for (auto& c : clients) if (c->selfId == id) return c.get();
        return nullptr;
    }

    // Schedule an event into `to`'s inbox, tagged as coming from `peer`.
    void Push(Endpoint& to, PeerId peer, NetEvent::Type type, std::vector<uint8_t> data, bool reliable, bool useOwnLink,
              Endpoint* linkOwner = nullptr) {
        const Link& l = useOwnLink ? to.link : (linkOwner ? linkOwner->link : to.link);
        float delay = l.latencySec + (l.jitterSec > 0 ? static_cast<float>(rng.Unit()) * l.jitterSec : 0.0f);
        if (type == NetEvent::Type::Data && !reliable && l.unreliableLoss > 0 && rng.Unit() < l.unreliableLoss) return;
        float at = (std::max)(now + delay, to.lastAt);
        to.lastAt = at;
        NetEvent ev;
        ev.type = type;
        ev.peer = peer;
        ev.data = std::move(data);
        to.inbox.push_back({at, std::move(ev)});
    }

    void Deliver(Endpoint& from, PeerId peer, const uint8_t* data, size_t size, bool reliable) {
        std::vector<uint8_t> bytes(data, data + size);
        if (from.isServer) {
            Endpoint* c = ClientFor(peer);
            if (!c || !c->open) return;
            bytesSentByServer += size;
            packetsSentByServer++;
            Push(*c, 0, NetEvent::Type::Data, std::move(bytes), reliable, false, c);
        } else {
            if (!from.open) return;
            Push(server, from.selfId, NetEvent::Type::Data, std::move(bytes), reliable, false, &from);
        }
    }

    // Like ENet, both ends get a Disconnected event whichever side hangs up.
    void Drop(Endpoint& by, PeerId peer) {
        Endpoint* c = by.isServer ? ClientFor(peer) : &by;
        if (!c || !c->open) return;
        c->open = false;
        Push(*c, 0, NetEvent::Type::Disconnected, {}, false, false, c);
        Push(server, c->selfId, NetEvent::Type::Disconnected, {}, false, false, c);
    }

    Rng rng;
    float now = 0;
    PeerId nextPeer = 1;
    Endpoint server;
    std::vector<std::unique_ptr<Endpoint>> clients;
};

} // namespace royale::net
