#include "UdpRawLogger.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace biem::net {

namespace fs = std::filesystem;

namespace {

std::string hexDump(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
        if (i + 1 < len) oss << ' ';
    }
    return oss.str();
}

std::string todayStamp() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y%m%d");
    return oss.str();
}

} // namespace

UdpRawLogger::UdpRawLogger(uint16_t listenPort, std::string logDir)
    : listenPort_(listenPort), logDir_(std::move(logDir)) {}

UdpRawLogger::~UdpRawLogger() {
    stop();
}

bool UdpRawLogger::start() {
    if (running_) return true;

    std::error_code ec;
    fs::create_directories(logDir_, ec);

#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    sock_ = static_cast<uintptr_t>(s);
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return false;
    sock_ = s;
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(listenPort_);

#if defined(_WIN32)
    if (bind(static_cast<SOCKET>(sock_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(static_cast<SOCKET>(sock_));
        WSACleanup();
        sock_ = 0;
        return false;
    }
#else
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock_);
        sock_ = -1;
        return false;
    }
#endif

    running_ = true;
    thread_ = std::thread([this]() { run(); });
    return true;
}

void UdpRawLogger::stop() {
    if (!running_) return;
    running_ = false;

#if defined(_WIN32)
    if (sock_) {
        closesocket(static_cast<SOCKET>(sock_));
        sock_ = 0;
    }
#else
    if (sock_ >= 0) {
        ::shutdown(sock_, SHUT_RDWR);
        ::close(sock_);
        sock_ = -1;
    }
#endif

    if (thread_.joinable()) thread_.join();

#if defined(_WIN32)
    WSACleanup();
#endif
}

void UdpRawLogger::run() {
    std::vector<uint8_t> buf(65536);

    while (running_) {
        sockaddr_in from{};

#if defined(_WIN32)
        int fromLen = sizeof(from);
        int n = recvfrom(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(buf.data()),
                          static_cast<int>(buf.size()), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n == SOCKET_ERROR) {
            if (!running_) break;
            continue;
        }
#else
        socklen_t fromLen = sizeof(from);
        ssize_t n = recvfrom(sock_, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n < 0) {
            if (!running_) break;
            continue;
        }
#endif
        if (n <= 0) continue;

        char ipStr[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));

        RawPacket pkt;
        pkt.unixTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        pkt.sourceIp = ipStr;
        pkt.sourcePort = ntohs(from.sin_port);
        pkt.data = buf.data();
        pkt.length = static_cast<size_t>(n);

        logPacket(pkt);
        if (cb_) cb_(pkt);
    }
}

void UdpRawLogger::logPacket(const RawPacket& pkt) {
    std::string stamp = todayStamp();

    std::ofstream hexFile(logDir_ + "/" + stamp + ".hexlog", std::ios::app);
    if (hexFile) {
        hexFile << pkt.unixTimeMs << " " << pkt.sourceIp << ":" << pkt.sourcePort << " len=" << pkt.length
                << " " << hexDump(pkt.data, pkt.length) << "\n";
    }

    std::ofstream rawFile(logDir_ + "/" + stamp + ".raw", std::ios::app | std::ios::binary);
    if (rawFile) {
        auto writeLE = [&](uint64_t v, int bytes) {
            for (int i = 0; i < bytes; ++i) {
                char b = static_cast<char>((v >> (8 * i)) & 0xFF);
                rawFile.write(&b, 1);
            }
        };
        writeLE(static_cast<uint64_t>(pkt.unixTimeMs), 8);
        writeLE(static_cast<uint64_t>(pkt.length), 4);
        rawFile.write(reinterpret_cast<const char*>(pkt.data), static_cast<std::streamsize>(pkt.length));
    }
}

} // namespace biem::net
