// voice_samp.cpp — minimalny plugin serwera bez AMX/SDK (auto-start UDP)

// ——— opcjonalnie, jeœli projekt ma PCH:
#if defined(__has_include)
#  if __has_include("pch.h")
#    include "pch.h"
#  endif
#endif

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

// ——— Minimalne definicje eksportów pluginu (tylko jeœli brak):
#ifndef PLUGIN_EXPORT
#  ifdef _WIN32
#    define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#    define PLUGIN_CALL __stdcall
#  else
#    define PLUGIN_EXPORT extern "C"
#    define PLUGIN_CALL
#  endif
#endif

// logprintf z serwera
typedef void (*logprintf_t)(const char* format, ...);
static logprintf_t logprintf_ = nullptr;

enum PLUGIN_DATA_TYPE {
    PLUGIN_DATA_LOGPRINTF = 0x00,
};

#define SUPPORTS_VERSION       0x0200
#define SUPPORTS_AMX_NATIVES   0x0202
#define SUPPORTS_PROCESS_TICK  0x0208

struct AMX; // forward-only – nie korzystamy z AMX

// ====== Protokó³ (musi byæ spójny z klientem .asi) ======
#pragma pack(push, 1)
struct VoiceHdr {
    uint32_t magic;    // 'VOIP' 0x50494F56
    uint16_t type;     // 1=HELLO, 2=AUDIO
    uint32_t senderId;
    uint32_t seq;
    uint16_t nameLen;
    uint16_t payloadLen;
};
#pragma pack(pop)
static const uint32_t MAGIC = 0x50494F56;

struct Peer {
    sockaddr_in addr{};  // zainicjalizowany
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

        VoiceHdr h{};
        memcpy(&h, buf.data(), sizeof(h));
        if (h.magic != MAGIC) continue;

        auto now = std::chrono::steady_clock::now();

        if (h.type == 1) { // HELLO
            if ((int)sizeof(h) + h.nameLen > n) continue;
            std::string name((const char*)buf.data() + sizeof(h), h.nameLen);

            Peer p;
            p.addr = from;
            p.name = name;
            p.last = now;
            g_peers[h.senderId] = std::move(p);

            if (logprintf_) logprintf_("[voice] HELLO %s [%u]", name.c_str(), h.senderId);
        }
        else if (h.type == 2) { // AUDIO
            if ((int)sizeof(h) + h.nameLen + h.payloadLen > n) continue;

            auto it = g_peers.find(h.senderId);
            if (it == g_peers.end()) {
                std::string name((const char*)buf.data() + sizeof(h), h.nameLen);
                Peer p;
                p.addr = from;
                p.name = name;
                p.last = now;
                g_peers[h.senderId] = std::move(p);
            }
            else {
                it->second.last = now;
            }

            // rozsy³ka do wszystkich pozosta³ych
            for (auto& kv : g_peers) {
                if (kv.first == h.senderId) continue;
                sendto(g_sock, (const char*)buf.data(), n, 0,
                    (sockaddr*)&kv.second.addr, sizeof(kv.second.addr));
            }
        }

        // cleanup nieaktywnych >5s
        for (auto it = g_peers.begin(); it != g_peers.end();) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last).count() > 5)
                it = g_peers.erase(it);
            else ++it;
        }
    }
}

// ====== Exporty pluginu ======
PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports() {
    return SUPPORTS_VERSION | SUPPORTS_PROCESS_TICK | SUPPORTS_AMX_NATIVES;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void** ppData) {
    logprintf_ = (logprintf_t)ppData[PLUGIN_DATA_LOGPRINTF];

#ifdef _WIN32
    WSADATA wsa{};
    int wret = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (wret != 0) {
        if (logprintf_) logprintf_("[voice] WSAStartup failed (%d)", wret);
        return false;
    }
#endif

    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock == INVALID_SOCKET) {
        if (logprintf_) logprintf_("[voice] socket() failed");
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(g_port);
    srv.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_sock, (sockaddr*)&srv, sizeof(srv)) != 0) {
        if (logprintf_) logprintf_("[voice] bind() failed on UDP %d", g_port);
        closesocket(g_sock); g_sock = INVALID_SOCKET;
#ifdef _WIN32
        WSACleanup();
#endif
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

    if (g_sock != INVALID_SOCKET) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif

    if (logprintf_) logprintf_("[voice] stopped");
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX* /*amx*/) { return 0; }
PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX* /*amx*/) { return 0; }
PLUGIN_EXPORT void PLUGIN_CALL ProcessTick() { /* no-op */ }
