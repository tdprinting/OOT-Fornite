#include "enet_transport.h"
#include <enet/enet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>

namespace royale::net {

namespace {
constexpr size_t kChannels = 2; // 0 = reliable, 1 = unreliable
int enetUsers = 0;

bool AcquireENet() {
    if (enetUsers == 0 && enet_initialize() != 0) return false;
    enetUsers++;
    return true;
}
void ReleaseENet() {
    if (--enetUsers == 0) enet_deinitialize();
}
// The operating system's reason for the most recent socket failure (ENet does not report one). Captured right after the
// failing call, before anything else can overwrite it.
std::string OsReason() {
#ifdef _WIN32
    return "Windows error " + std::to_string(WSAGetLastError());
#else
    int e = errno;
    return std::string(std::strerror(e)) + " (errno " + std::to_string(e) + ")";
#endif
}
void SetError(std::string* error, const std::string& msg) {
    if (error) *error = msg;
}
// A host that stops answering is dropped after about 8 s; a connection attempt gives up after about 3 s.
void ConfigureTimeouts(ENetPeer* peer, bool connecting) {
    if (connecting) enet_peer_timeout(peer, 16, 1000, 3000);
    else enet_peer_timeout(peer, 32, 2000, 8000);
}
} // namespace

std::unique_ptr<ENetTransport> ENetTransport::Host(uint16_t port, size_t maxClients, std::string* error) {
    if (!AcquireENet()) { SetError(error, "enet_initialize failed"); return nullptr; }
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    ENetHost* h = enet_host_create(&addr, maxClients, kChannels, 0, 0);
    if (!h) {
        std::string why = OsReason();
        ReleaseENet();
        // Common causes: the port is already in use, or (Android) the app lacks the INTERNET permission.
        SetError(error, "could not listen on port " + std::to_string(port) + ": " + why);
        return nullptr;
    }
    std::unique_ptr<ENetTransport> t(new ENetTransport());
    t->host = h;
    t->isServer = true;
    ENetAddress bound;
    t->port = enet_socket_get_address(h->socket, &bound) == 0 ? bound.port : port;
    return t;
}

std::unique_ptr<ENetTransport> ENetTransport::Connect(const std::string& hostName, uint16_t port, std::string* error) {
    if (!AcquireENet()) { SetError(error, "enet_initialize failed"); return nullptr; }
    ENetHost* h = enet_host_create(nullptr, 1, kChannels, 0, 0);
    if (!h) {
        std::string why = OsReason();
        ReleaseENet();
        SetError(error, "could not create a network socket: " + why);
        return nullptr;
    }
    ENetAddress addr;
    if (enet_address_set_host(&addr, hostName.c_str()) != 0) {
        enet_host_destroy(h);
        ReleaseENet();
        SetError(error, "could not resolve host name");
        return nullptr;
    }
    addr.port = port;
    ENetPeer* peer = enet_host_connect(h, &addr, kChannels, 0);
    if (!peer) {
        enet_host_destroy(h);
        ReleaseENet();
        SetError(error, "could not start connecting");
        return nullptr;
    }
    ConfigureTimeouts(peer, true);
    std::unique_ptr<ENetTransport> t(new ENetTransport());
    t->host = h;
    t->isServer = false;
    t->peers[0] = peer;
    peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(0));
    return t;
}

ENetTransport::~ENetTransport() {
    if (!host) return;
    for (auto& [id, peer] : peers) enet_peer_disconnect_now(peer, 0);
    enet_host_flush(host);
    enet_host_destroy(host);
    ReleaseENet();
}

void ENetTransport::Pump() {
    ENetEvent ev;
    while (enet_host_service(host, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                PeerId id = 0;
                if (isServer) {
                    id = nextPeer++;
                    peers[id] = ev.peer;
                    ev.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
                    ConfigureTimeouts(ev.peer, false);
                } else {
                    ConfigureTimeouts(ev.peer, false);
                }
                NetEvent e;
                e.type = NetEvent::Type::Connected;
                e.peer = id;
                queue.push_back(std::move(e));
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                NetEvent e;
                e.type = NetEvent::Type::Data;
                e.peer = static_cast<PeerId>(reinterpret_cast<uintptr_t>(ev.peer->data));
                e.data.assign(ev.packet->data, ev.packet->data + ev.packet->dataLength);
                enet_packet_destroy(ev.packet);
                queue.push_back(std::move(e));
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                PeerId id = static_cast<PeerId>(reinterpret_cast<uintptr_t>(ev.peer->data));
                peers.erase(id);
                NetEvent e;
                e.type = NetEvent::Type::Disconnected;
                e.peer = id;
                queue.push_back(std::move(e));
                break;
            }
            default:
                break;
        }
    }
}

bool ENetTransport::Poll(NetEvent& out) {
    if (queue.empty()) Pump();
    if (queue.empty()) return false;
    out = std::move(queue.front());
    queue.pop_front();
    return true;
}

void ENetTransport::Send(PeerId peer, const uint8_t* data, size_t size, bool reliable) {
    auto it = peers.find(peer);
    if (it == peers.end()) return;
    ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet) return;
    if (enet_peer_send(it->second, reliable ? 0 : 1, packet) < 0) enet_packet_destroy(packet);
}

void ENetTransport::Disconnect(PeerId peer) {
    auto it = peers.find(peer);
    if (it == peers.end()) return;
    // "later": queued reliable messages (e.g. a Reject) are delivered first.
    enet_peer_disconnect_later(it->second, 0);
}

void ENetTransport::Flush() {
    if (host) enet_host_flush(host);
}

} // namespace royale::net
