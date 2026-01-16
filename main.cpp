// main.cpp — Multi-client TCP echo server (Winsock)
// Build (MSVC): link ws2_32.lib
// Run: start EXE, connect with:  telnet 127.0.0.1 54000  (or netcat)

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

static std::atomic<bool> g_running{true};
static SOCKET g_listenSocket = INVALID_SOCKET;
static std::mutex g_logMutex;

static void logLine(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cout << s << std::endl;
}

static std::string lastWsaError(const char* what) {
    return std::string(what) + " (WSAGetLastError=" + std::to_string(WSAGetLastError()) + ")";
}

static bool sendAll(SOCKET s, const char* data, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(s, data + total, len - total, 0);
        if (sent == SOCKET_ERROR) return false;
        if (sent == 0) return false;
        total += sent;
    }
    return true;
}

static void clientThread(SOCKET clientSocket, sockaddr_in clientAddr) {
    char host[NI_MAXHOST]{};
    char service[NI_MAXSERV]{};

    if (getnameinfo(reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr),
                    host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0) {
        logLine(std::string("[+] Client connected: ") + host + ":" + service);
    } else {
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, INET_ADDRSTRLEN);
        logLine(std::string("[+] Client connected: ") + ip + ":" + std::to_string(ntohs(clientAddr.sin_port)));
    }

    constexpr int BUF_SIZE = 4096;
    char buf[BUF_SIZE];

    while (g_running.load()) {
        int bytes = recv(clientSocket, buf, BUF_SIZE, 0);

        if (bytes == 0) {
            logLine("[-] Client disconnected");
            break;
        }
        if (bytes == SOCKET_ERROR) {
            logLine(lastWsaError("[-] recv() failed"));
            break;
        }

        // Echo back exactly what we received
        if (!sendAll(clientSocket, buf, bytes)) {
            logLine(lastWsaError("[-] send() failed"));
            break;
        }
    }

    closesocket(clientSocket);
}

static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_running.store(false);
            if (g_listenSocket != INVALID_SOCKET) {
                closesocket(g_listenSocket); // force accept() to unblock
                g_listenSocket = INVALID_SOCKET;
            }
            return TRUE;
        default:
            return FALSE;
    }
}

int main() {
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    WSADATA wsData{};
    WORD ver = MAKEWORD(2, 2);
    if (WSAStartup(ver, &wsData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET) {
        std::cerr << lastWsaError("socket() failed") << "\n";
        WSACleanup();
        return 1;
    }

    // Allow quick restart after close (avoid "address already in use")
    BOOL reuse = TRUE;
    setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in hint{};
    hint.sin_family = AF_INET;
    hint.sin_port = htons(54000);
    hint.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_listenSocket, reinterpret_cast<sockaddr*>(&hint), sizeof(hint)) == SOCKET_ERROR) {
        std::cerr << lastWsaError("bind() failed") << "\n";
        closesocket(g_listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(g_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << lastWsaError("listen() failed") << "\n";
        closesocket(g_listenSocket);
        WSACleanup();
        return 1;
    }

    logLine("[*] Server listening on 0.0.0.0:54000");

    while (g_running.load()) {
        sockaddr_in client{};
        int clientSize = sizeof(client);

        SOCKET clientSocket = accept(g_listenSocket, reinterpret_cast<sockaddr*>(&client), &clientSize);
        if (!g_running.load()) break; // shutdown path

        if (clientSocket == INVALID_SOCKET) {
            // If we’re shutting down, accept can fail because we closed the listening socket
            if (!g_running.load()) break;
            logLine(lastWsaError("accept() failed"));
            continue;
        }

        // Thread-per-client (simple + effective for a learning server)
        std::thread(clientThread, clientSocket, client).detach();
    }

    logLine("[*] Shutting down...");
    if (g_listenSocket != INVALID_SOCKET) closesocket(g_listenSocket);
    WSACleanup();
    return 0;
}
