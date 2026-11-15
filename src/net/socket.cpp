// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "net/socket.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <vector>

namespace xc::net {

#if defined(_WIN32)

static std::once_flag g_wsaInitOnce;
static bool g_wsaInitialized = false;

bool socketsInit() {
    std::call_once(g_wsaInitOnce, [] {
        WSADATA wsaData;
        int rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
        g_wsaInitialized = (rc == 0);
    });
    return g_wsaInitialized;
}

void socketsShutdown() {
    if (g_wsaInitialized) {
        WSACleanup();
        g_wsaInitialized = false;
    }
}

Socket socketCreate(int domain, int type, int protocol) {
    socketsInit();
    SOCKET s = ::socket(domain, type, protocol);
    if (s == INVALID_SOCKET) {
        return kInvalidSocket;
    }
    return Socket{static_cast<intptr_t>(s)};
}

void closeSocket(Socket& s) {
    if (!s.valid()) return;
    ::closesocket(static_cast<SOCKET>(s.fd));
    s.fd = -1;
}

bool shutdownSocket(Socket s, int how) {
    if (!s.valid()) return false;
    // Map POSIX SHUT_RD (0), SHUT_WR (1), SHUT_RDWR (2) to SD_RECEIVE (0), SD_SEND (1), SD_BOTH (2)
    return ::shutdown(static_cast<SOCKET>(s.fd), how) == 0;
}

bool connectSocket(Socket s, const sockaddr* addr, socklen_t addrlen) {
    if (!s.valid()) return false;
    return ::connect(static_cast<SOCKET>(s.fd), addr, static_cast<int>(addrlen)) == 0;
}

bool connectSocket(Socket s, const std::string& ip, int port) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    if (ip == "127.0.0.1" || ip == "localhost") {
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        if (::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) return false;
    }
    return connectSocket(s, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
}

bool bindSocket(Socket s, const sockaddr* addr, socklen_t addrlen) {
    if (!s.valid()) return false;
    return ::bind(static_cast<SOCKET>(s.fd), addr, static_cast<int>(addrlen)) == 0;
}

bool bindSocket(Socket s, const std::string& ip, int port) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    if (ip == "127.0.0.1" || ip == "localhost") {
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else if (ip == "0.0.0.0" || ip.empty()) {
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) return false;
    }
    return bindSocket(s, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
}

bool listenSocket(Socket s, int backlog) {
    if (!s.valid()) return false;
    return ::listen(static_cast<SOCKET>(s.fd), backlog) == 0;
}

Socket acceptSocket(Socket s, sockaddr* addr, socklen_t* addrlen) {
    if (!s.valid()) return kInvalidSocket;
    int len = addrlen ? static_cast<int>(*addrlen) : 0;
    SOCKET client = ::accept(static_cast<SOCKET>(s.fd), addr, addrlen ? &len : nullptr);
    if (addrlen) *addrlen = static_cast<socklen_t>(len);
    if (client == INVALID_SOCKET) return kInvalidSocket;
    return Socket{static_cast<intptr_t>(client)};
}

bool getSocketPort(Socket s, int& outPort) {
    if (!s.valid()) return false;
    sockaddr_in sa{};
    int len = sizeof(sa);
    if (::getsockname(static_cast<SOCKET>(s.fd), reinterpret_cast<sockaddr*>(&sa), &len) == 0) {
        outPort = ntohs(sa.sin_port);
        return true;
    }
    return false;
}

bool setNonBlocking(Socket s, bool nb) {
    if (!s.valid()) return false;
    u_long mode = nb ? 1 : 0;
    return ::ioctlsocket(static_cast<SOCKET>(s.fd), FIONBIO, &mode) == 0;
}

bool setRecvTimeout(Socket s, int timeoutMs) {
    if (!s.valid()) return false;
    DWORD ms = static_cast<DWORD>(timeoutMs >= 0 ? timeoutMs : 0);
    return ::setsockopt(static_cast<SOCKET>(s.fd), SOL_SOCKET, SO_RCVTIMEO,
                        reinterpret_cast<const char*>(&ms), sizeof(ms)) == 0;
}

bool setSendTimeout(Socket s, int timeoutMs) {
    if (!s.valid()) return false;
    DWORD ms = static_cast<DWORD>(timeoutMs >= 0 ? timeoutMs : 0);
    return ::setsockopt(static_cast<SOCKET>(s.fd), SOL_SOCKET, SO_SNDTIMEO,
                        reinterpret_cast<const char*>(&ms), sizeof(ms)) == 0;
}

bool setTcpNoDelay(Socket s, bool on) {
    if (!s.valid()) return false;
    int val = on ? 1 : 0;
    return ::setsockopt(static_cast<SOCKET>(s.fd), IPPROTO_TCP, TCP_NODELAY,
                        reinterpret_cast<const char*>(&val), sizeof(val)) == 0;
}

bool setReuseOrExclusiveAddr(Socket s) {
    if (!s.valid()) return false;
    // On Windows, SO_EXCLUSIVEADDRUSE prevents port hijacking
    int val = 1;
    return ::setsockopt(static_cast<SOCKET>(s.fd), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                        reinterpret_cast<const char*>(&val), sizeof(val)) == 0;
}

bool setRecvBufferSize(Socket s, int bytes) {
    if (!s.valid()) return false;
    return ::setsockopt(static_cast<SOCKET>(s.fd), SOL_SOCKET, SO_RCVBUF,
                        reinterpret_cast<const char*>(&bytes), sizeof(bytes)) == 0;
}

bool setNoSigpipe(Socket s) {
    (void)s;
    return true; // No SIGPIPE on Windows
}

bool getSocketError(Socket s, int& outErr) {
    if (!s.valid()) { outErr = WSAENOTSOCK; return false; }
    int err = 0;
    int len = sizeof(err);
    int rc = ::getsockopt(static_cast<SOCKET>(s.fd), SOL_SOCKET, SO_ERROR,
                          reinterpret_cast<char*>(&err), &len);
    outErr = err;
    return rc == 0;
}

bool bytesAvailable(Socket s, size_t& outBytes) {
    if (!s.valid()) return false;
    u_long count = 0;
    if (::ioctlsocket(static_cast<SOCKET>(s.fd), FIONREAD, &count) == 0) {
        outBytes = static_cast<size_t>(count);
        return true;
    }
    return false;
}

int pollSockets(Socket* readSocks, size_t numRead, Socket* writeSocks, size_t numWrite, int timeoutMs) {
    // select() on Windows operates on fd_set which is an array of SOCKETs (not bitmask).
    // It avoids WSAPoll bugs with connect rejection prior to Windows 10 2004.
    fd_set rset, wset, eset;
    FD_ZERO(&rset);
    FD_ZERO(&wset);
    FD_ZERO(&eset);

    size_t rCount = 0, wCount = 0;
    for (size_t i = 0; i < numRead && rCount < FD_SETSIZE; ++i) {
        if (readSocks && readSocks[i].valid()) {
            FD_SET(static_cast<SOCKET>(readSocks[i].fd), &rset);
            ++rCount;
        }
    }
    for (size_t i = 0; i < numWrite && wCount < FD_SETSIZE; ++i) {
        if (writeSocks && writeSocks[i].valid()) {
            SOCKET ws = static_cast<SOCKET>(writeSocks[i].fd);
            FD_SET(ws, &wset);
            FD_SET(ws, &eset); // Catch connection failures
            ++wCount;
        }
    }
    if (rCount == 0 && wCount == 0) return 0;

    timeval tv;
    timeval* ptv = nullptr;
    if (timeoutMs >= 0) {
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        ptv = &tv;
    }

    int rc = ::select(0, rCount > 0 ? &rset : nullptr,
                         wCount > 0 ? &wset : nullptr,
                         wCount > 0 ? &eset : nullptr,
                         ptv);
    return rc;
}

ssize_t sendNoSignal(Socket s, const void* buf, size_t len) {
    if (!s.valid()) return -1;
    int n = ::send(static_cast<SOCKET>(s.fd), static_cast<const char*>(buf),
                   static_cast<int>(len), 0);
    return static_cast<ssize_t>(n);
}

ssize_t recvSocket(Socket s, void* buf, size_t len, int flags) {
    if (!s.valid()) return -1;
    int n = ::recv(static_cast<SOCKET>(s.fd), static_cast<char*>(buf),
                   static_cast<int>(len), flags);
    return static_cast<ssize_t>(n);
}

ssize_t sendToSocket(Socket s, const void* buf, size_t len, int flags, const sockaddr* to, socklen_t tolen) {
    if (!s.valid()) return -1;
    int n = ::sendto(static_cast<SOCKET>(s.fd), static_cast<const char*>(buf),
                     static_cast<int>(len), flags, to, static_cast<int>(tolen));
    return static_cast<ssize_t>(n);
}

ssize_t recvFromSocket(Socket s, void* buf, size_t len, int flags, sockaddr* from, socklen_t* fromlen) {
    if (!s.valid()) return -1;
    int flen = fromlen ? static_cast<int>(*fromlen) : 0;
    int n = ::recvfrom(static_cast<SOCKET>(s.fd), static_cast<char*>(buf),
                       static_cast<int>(len), flags, from, fromlen ? &flen : nullptr);
    if (fromlen) *fromlen = static_cast<socklen_t>(flen);
    return static_cast<ssize_t>(n);
}

int lastSocketError() {
    return ::WSAGetLastError();
}

bool wouldBlock(int err) {
    return err == WSAEWOULDBLOCK || err == WSAETIMEDOUT;
}

bool inProgress(int err) {
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
}

bool wasInterrupted(int err) {
    return err == WSAEINTR;
}

std::string formatSocketError(int err) {
    char buf[256];
    DWORD len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, static_cast<DWORD>(err),
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               buf, sizeof(buf), nullptr);
    if (len > 0) {
        while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' ')) {
            buf[--len] = '\0';
        }
        return std::string(buf, len) + " (" + std::to_string(err) + ")";
    }
    return "WSA error " + std::to_string(err);
}

#else  // POSIX (PlayStation 5, PlayStation 4, Linux, Android)

bool socketsInit() { return true; }
void socketsShutdown() {}

Socket socketCreate(int domain, int type, int protocol) {
    int fd = ::socket(domain, type, protocol);
    if (fd < 0) return kInvalidSocket;
    return Socket{static_cast<intptr_t>(fd)};
}

void closeSocket(Socket& s) {
    if (!s.valid()) return;
    ::close(static_cast<int>(s.fd));
    s.fd = -1;
}

bool shutdownSocket(Socket s, int how) {
    if (!s.valid()) return false;
    return ::shutdown(static_cast<int>(s.fd), how) == 0;
}

bool connectSocket(Socket s, const sockaddr* addr, socklen_t addrlen) {
    if (!s.valid()) return false;
    return ::connect(static_cast<int>(s.fd), addr, addrlen) == 0;
}

bool connectSocket(Socket s, const std::string& ip, int port) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    if (ip == "127.0.0.1" || ip == "localhost") {
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        if (::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) return false;
    }
    return connectSocket(s, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
}

bool bindSocket(Socket s, const sockaddr* addr, socklen_t addrlen) {
    if (!s.valid()) return false;
    return ::bind(static_cast<int>(s.fd), addr, addrlen) == 0;
}

bool bindSocket(Socket s, const std::string& ip, int port) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    if (ip == "127.0.0.1" || ip == "localhost") {
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else if (ip == "0.0.0.0" || ip.empty()) {
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) return false;
    }
    return bindSocket(s, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
}

bool listenSocket(Socket s, int backlog) {
    if (!s.valid()) return false;
    return ::listen(static_cast<int>(s.fd), backlog) == 0;
}

Socket acceptSocket(Socket s, sockaddr* addr, socklen_t* addrlen) {
    if (!s.valid()) return kInvalidSocket;
    int client = ::accept(static_cast<int>(s.fd), addr, addrlen);
    if (client < 0) return kInvalidSocket;
    return Socket{static_cast<intptr_t>(client)};
}

bool getSocketPort(Socket s, int& outPort) {
    if (!s.valid()) return false;
    sockaddr_in sa{};
    socklen_t len = sizeof(sa);
    if (::getsockname(static_cast<int>(s.fd), reinterpret_cast<sockaddr*>(&sa), &len) == 0) {
        outPort = ntohs(sa.sin_port);
        return true;
    }
    return false;
}

bool setNonBlocking(Socket s, bool nb) {
    if (!s.valid()) return false;
    // On PS5/POSIX, ioctl(FIONBIO) is the tested and functional method
    int mode = nb ? 1 : 0;
    return ::ioctl(static_cast<int>(s.fd), FIONBIO, &mode) == 0;
}

bool setRecvTimeout(Socket s, int timeoutMs) {
    if (!s.valid()) return false;
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return ::setsockopt(static_cast<int>(s.fd), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

bool setSendTimeout(Socket s, int timeoutMs) {
    if (!s.valid()) return false;
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return ::setsockopt(static_cast<int>(s.fd), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

bool setTcpNoDelay(Socket s, bool on) {
    if (!s.valid()) return false;
    int val = on ? 1 : 0;
    return ::setsockopt(static_cast<int>(s.fd), IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == 0;
}

bool setReuseOrExclusiveAddr(Socket s) {
    if (!s.valid()) return false;
    int val = 1;
    return ::setsockopt(static_cast<int>(s.fd), SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == 0;
}

bool setRecvBufferSize(Socket s, int bytes) {
    if (!s.valid()) return false;
    return ::setsockopt(static_cast<int>(s.fd), SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0;
}

bool setNoSigpipe(Socket s) {
    if (!s.valid()) return false;
#if defined(SO_NOSIGPIPE)
    int one = 1;
    return ::setsockopt(static_cast<int>(s.fd), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) == 0;
#else
    return true;
#endif
}

bool getSocketError(Socket s, int& outErr) {
    if (!s.valid()) { outErr = EBADF; return false; }
    int err = 0;
    socklen_t len = sizeof(err);
    int rc = ::getsockopt(static_cast<int>(s.fd), SOL_SOCKET, SO_ERROR, &err, &len);
    outErr = err;
    return rc == 0;
}

bool bytesAvailable(Socket s, size_t& outBytes) {
    if (!s.valid()) return false;
    int count = 0;
    if (::ioctl(static_cast<int>(s.fd), FIONREAD, &count) == 0) {
        outBytes = (count >= 0) ? static_cast<size_t>(count) : 0;
        return true;
    }
    return false;
}

int pollSockets(Socket* readSocks, size_t numRead, Socket* writeSocks, size_t numWrite, int timeoutMs) {
    size_t total = (readSocks ? numRead : 0) + (writeSocks ? numWrite : 0);
    if (total == 0) return 0;
    pollfd stackPfds[16];
    std::vector<pollfd> heapPfds;
    pollfd* pfds = (total <= 16) ? stackPfds : (heapPfds.resize(total), heapPfds.data());

    size_t idx = 0;
    for (size_t i = 0; i < numRead && readSocks; ++i) {
        pfds[idx].fd = static_cast<int>(readSocks[i].fd);
        pfds[idx].events = POLLIN;
        pfds[idx].revents = 0;
        ++idx;
    }
    for (size_t i = 0; i < numWrite && writeSocks; ++i) {
        pfds[idx].fd = static_cast<int>(writeSocks[i].fd);
        pfds[idx].events = POLLOUT;
        pfds[idx].revents = 0;
        ++idx;
    }

    int rc = ::poll(pfds, static_cast<nfds_t>(idx), timeoutMs);
    return rc;
}

ssize_t sendNoSignal(Socket s, const void* buf, size_t len) {
    if (!s.valid()) return -1;
    int flags = 0;
#if defined(MSG_NOSIGNAL) && (MSG_NOSIGNAL != 0)
    flags = MSG_NOSIGNAL;
#endif
    return ::send(static_cast<int>(s.fd), buf, len, flags);
}

ssize_t recvSocket(Socket s, void* buf, size_t len, int flags) {
    if (!s.valid()) return -1;
    return ::recv(static_cast<int>(s.fd), buf, len, flags);
}

ssize_t sendToSocket(Socket s, const void* buf, size_t len, int flags, const sockaddr* to, socklen_t tolen) {
    if (!s.valid()) return -1;
    return ::sendto(static_cast<int>(s.fd), buf, len, flags, to, tolen);
}

ssize_t recvFromSocket(Socket s, void* buf, size_t len, int flags, sockaddr* from, socklen_t* fromlen) {
    if (!s.valid()) return -1;
    return ::recvfrom(static_cast<int>(s.fd), buf, len, flags, from, fromlen);
}

int lastSocketError() {
    return errno;
}

bool wouldBlock(int err) {
    return err == EAGAIN || err == EWOULDBLOCK;
}

bool inProgress(int err) {
    return err == EINPROGRESS || err == EWOULDBLOCK;
}

bool wasInterrupted(int err) {
    return err == EINTR;
}

namespace {
[[maybe_unused]] inline const char* strerrorResult(int rc, const char* buf) {
    return rc == 0 ? buf : "unknown error";
}
[[maybe_unused]] inline const char* strerrorResult(const char* ptr, const char*) {
    return ptr ? ptr : "unknown error";
}
}  // namespace

std::string formatSocketError(int err) {
    char buf[128] = {0};
    const char* msg = strerrorResult(strerror_r(err, buf, sizeof(buf)), buf);
    return std::string(msg) + " (errno " + std::to_string(err) + ")";
}

#endif  // _WIN32

bool wouldBlock() { return wouldBlock(lastSocketError()); }
bool inProgress() { return inProgress(lastSocketError()); }
bool wasInterrupted() { return wasInterrupted(lastSocketError()); }

bool parseIpv6Address(const std::string& str, void* outAddr16) {
    return ::inet_pton(AF_INET6, str.c_str(), outAddr16) == 1;
}

bool parseIpv4Address(const std::string& str, void* outAddr4) {
    return ::inet_pton(AF_INET, str.c_str(), outAddr4) == 1;
}

}  // namespace xc::net
