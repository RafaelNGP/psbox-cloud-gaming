// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Portable network socket layer abstracting BSD sockets and Winsock2.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <basetsd.h>
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace xc::net {

// Portable socket handle wrapper matching M2 contract in PROJECT.md
struct Socket {
    intptr_t fd = -1;

    constexpr Socket() = default;
    constexpr explicit Socket(intptr_t handle) : fd(handle) {}

    bool valid() const { return fd != -1; }
    explicit operator bool() const { return valid(); }
    bool operator==(const Socket& o) const { return fd == o.fd; }
    bool operator!=(const Socket& o) const { return fd != o.fd; }
    bool operator<(const Socket& o) const { return fd < o.fd; }
};

constexpr Socket kInvalidSocket{-1};

// Subsystem lifecycle
bool socketsInit();
void socketsShutdown();

// Socket creation and lifecycle
Socket socketCreate(int domain, int type, int protocol = 0);
inline Socket createTcpSocket() { return socketCreate(AF_INET, SOCK_STREAM, 0); }
inline Socket createUdpSocket() { return socketCreate(AF_INET, SOCK_DGRAM, 0); }
void closeSocket(Socket& s);
inline void closeSocket(Socket&& s) { Socket tmp = s; closeSocket(tmp); }
bool shutdownSocket(Socket s, int how = 2 /* SHUT_RDWR on POSIX / SD_BOTH on Windows */);

// Connection and address binding
bool connectSocket(Socket s, const sockaddr* addr, socklen_t addrlen);
bool connectSocket(Socket s, const std::string& ip, int port);
bool bindSocket(Socket s, const sockaddr* addr, socklen_t addrlen);
bool bindSocket(Socket s, const std::string& ip, int port);
bool listenSocket(Socket s, int backlog = 8);
Socket acceptSocket(Socket s, sockaddr* addr = nullptr, socklen_t* addrlen = nullptr);
bool getSocketPort(Socket s, int& outPort);

// Socket options and modes
bool setNonBlocking(Socket s, bool nb);
bool setRecvTimeout(Socket s, int timeoutMs);
bool setSendTimeout(Socket s, int timeoutMs);
bool setTcpNoDelay(Socket s, bool on);
bool setReuseOrExclusiveAddr(Socket s);
bool setRecvBufferSize(Socket s, int bytes);
bool setNoSigpipe(Socket s);
bool getSocketError(Socket s, int& outErr);
bool bytesAvailable(Socket s, size_t& outBytes);

// Polling and multiplexing
// Returns >0 if ready events occurred, 0 on timeout, <0 on error
int pollSockets(Socket* readSocks, size_t numRead, Socket* writeSocks, size_t numWrite, int timeoutMs);

// I/O operations
ssize_t sendNoSignal(Socket s, const void* buf, size_t len);
ssize_t recvSocket(Socket s, void* buf, size_t len, int flags = 0);
ssize_t sendToSocket(Socket s, const void* buf, size_t len, int flags, const sockaddr* to, socklen_t tolen);
ssize_t recvFromSocket(Socket s, void* buf, size_t len, int flags, sockaddr* from, socklen_t* fromlen);

// Error code translation & diagnostics
int  lastSocketError();
bool wouldBlock(int err);
bool wouldBlock();
bool inProgress(int err);
bool inProgress();
bool wasInterrupted(int err);
bool wasInterrupted();
std::string formatSocketError(int err);

// Address parsing helpers
bool parseIpv6Address(const std::string& str, void* outAddr16);
bool parseIpv4Address(const std::string& str, void* outAddr4);

// High-level connect helper
Socket connectTcpSocket(const std::string& host, int port, int timeoutMs, std::string& err);

}  // namespace xc::net
