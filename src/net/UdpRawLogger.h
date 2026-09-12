#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace biem::net {

// `data` points into a buffer owned by UdpRawLogger's receive loop and is
// only valid for the duration of the packet callback - copy it if you need
// to keep it around.
struct RawPacket {
    int64_t unixTimeMs = 0;
    std::string sourceIp;
    uint16_t sourcePort = 0;
    const uint8_t* data = nullptr;
    size_t length = 0;
};

// Generic UDP listener that logs every received packet (timestamp + hex +
// raw binary, one file pair per day) to disk, and optionally forwards it
// to a callback. This is NOT protocol-specific - it exists so real traffic
// from a repeater (e.g. Hytera HR659) can be captured for protocol
// reverse-engineering or cross-checking against vendor documentation. See
// docs/HYTERA_HR659.md. This part of the codebase is fully functional
// today, independent of any DMR-decode open questions elsewhere.
class UdpRawLogger {
public:
    // Packets are appended to <logDir>/<yyyymmdd>.hexlog (human-readable
    // text: timestamp, source, length, hex) and <logDir>/<yyyymmdd>.raw
    // (binary: repeated [8-byte LE unix-ms timestamp][4-byte LE length]
    // [payload] records, so a later tool can split packets back apart
    // without a separate index file).
    UdpRawLogger(uint16_t listenPort, std::string logDir);
    ~UdpRawLogger();

    UdpRawLogger(const UdpRawLogger&) = delete;
    UdpRawLogger& operator=(const UdpRawLogger&) = delete;

    void setPacketCallback(std::function<void(const RawPacket&)> cb) { cb_ = std::move(cb); }

    bool start();
    void stop();
    bool isRunning() const { return running_; }

private:
    uint16_t listenPort_;
    std::string logDir_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::function<void(const RawPacket&)> cb_;

#if defined(_WIN32)
    uintptr_t sock_ = 0; // SOCKET (UINT_PTR) - kept as a plain integer so <winsock2.h> isn't needed here
#else
    int sock_ = -1;
#endif

    void run();
    void logPacket(const RawPacket& pkt);
};

} // namespace biem::net
