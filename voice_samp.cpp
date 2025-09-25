// voice_samp.cpp — minimalny plugin bez SDK (auto start UDP)

#include <thread>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

#ifndef PLUGIN_EXPORT
#ifdef _WIN32
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#define PLUGIN_CALL __stdcall
#else
#define PLUGIN_EXPORT extern "C"
#define PLUGIN_CALL
#endif
#endif

typedef void (*logprintf_t)(const char* format, ...);
static logprintf_t logprintf_ = nullptr;

#pragma pack(push, 1)
struct VoiceHdr {
    uint32_t magic;
    uint16_t type;     // 1=HELLO, 2=AUDIO
    uint32_t senderId;
    uint32_t seq;
    uint16_t nameLen;
    uint16_t payloadLen;
};
#pragma pack(pop)
static const uint32_t MAGIC = 0x50494F56;

struct Peer {
    sockaddr_in addr{};
    std::string name;
    std::chrono::steady_clock::time_point last{};
};

static std::atomic<bool> g_run(false);
static std::thread g_thr;
static SOCKET g_sock = INVALID_SOCKET;
static int g_port = 40320;
static std::unordered_map<uint32_t, Peer> g_peers;

static void udp_thread() {
    std::vector<uint8_t> buf(4096);
    sockaddr_in from{}; socklen_t flen = sizeof(from);

    while (g_run.load()) {
        int n = recvfrom(g_sock, (char*)buf.data(), (int)buf.size(), 0,
            (sockaddr*)&from, &flen);
        if (n <= 0) {
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif
            continue;
        }
        if (n < (int)sizeof(VoiceHdr)) continue;
        VoiceHdr h{}; memcpy(&h, buf.data(), sizeof(h));
        if (h.magic != MAGIC) continue;

        auto now = std::chrono::steady_clock::now();

        if (h.type == 1) {
            std::string name((const char*)buf.data() + sizeof(h), h.nameLen);
            g_peers[h.senderId] = Peer{ from, name, now };
            if (logprintf_) logprintf_("[voice] HELLO %s [%u]", name.c_str(), h.senderId);
        }
        else if (h.type == 2) {
            std::string name((const char*)buf.data() + sizeof(h), h.nameLen);
            auto it = g_peers.find(h.senderId);
            if (it == g_peers.end()) {
                g_peers[h.senderId] = Peer{ from, name, now };
            }
            else {
                it->second.last = now;
            }

            // 🔎 logowanie AUDIO
            if (logprintf_) {
                logprintf_("[voice] AUDIO from %s [%u] len=%d",
                    name.c_str(), h.senderId, h.payloadLen);
            }

            // rozsyłanie do innych peerów
            for (auto& kv : g_peers) {
                if (kv.first == h.senderId) continue;
                sendto(g_sock, (const char*)buf.data(), n, 0,
                    (sockaddr*)&kv.second.addr, sizeof(kv.second.addr));
            }
        }

        // cleanup: peer offline > 5s
        for (auto it = g_peers.begin(); it != g_peers.end();) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last).count() > 5)
                it = g_peers.erase(it);
            else ++it;
        }
    }
}

// ================= entrypointy pluginu =================
PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports() {
    return 0; // nie używamy AMX
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void** ppData) {
    logprintf_ = (logprintf_t)ppData[0]; // [0] = logprintf
#ifdef _WIN32
    WSADATA wsa{}; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock == INVALID_SOCKET) return false;

    sockaddr_in srv{}; srv.sin_family = AF_INET;
    srv.sin_port = htons(g_port);
    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_sock, (sockaddr*)&srv, sizeof(srv)) != 0) {
        closesocket(g_sock); g_sock = INVALID_SOCKET;
        return false;
    }
#ifndef _WIN32
    fcntl(g_sock, F_SETFL, O_NONBLOCK);
#endif
    g_run = true;
    g_thr = std::thread(udp_thread);
    if (logprintf_) logprintf_("[voice] listening on UDP %d", g_port);
    return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload() {
    g_run = false;
    if (g_thr.joinable()) g_thr.join();
    if (g_sock != INVALID_SOCKET) closesocket(g_sock);
#ifdef _WIN32
    WSACleanup();
#endif
    if (logprintf_) logprintf_("[voice] stopped");
}
