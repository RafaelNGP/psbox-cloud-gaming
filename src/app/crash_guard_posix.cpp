// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/crash_guard.h"

#include "platform/platform.h"
#include "util/log.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

namespace xc::app::crash {

namespace {

char g_marker[512] = {};
char g_stage[96] = "starting";
std::atomic<uint64_t> g_beat{0};
uintptr_t g_ebootBase = 0;
uintptr_t g_ebootEnd = 0;

void initBase() {
    platform::getCodeBounds(g_ebootBase, g_ebootEnd);
}

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default: return "?";
    }
}

void appendStr(char* buf, size_t cap, size_t& len, const char* s) {
    if (!s) return;
    while (*s && len + 1 < cap) buf[len++] = *s++;
    buf[len] = 0;
}

void appendDec(char* buf, size_t cap, size_t& len, int val) {
    char tmp[16];
    int n = 0;
    unsigned int v = val < 0 ? static_cast<unsigned int>(-val) : static_cast<unsigned int>(val);
    if (val < 0 && len + 1 < cap) buf[len++] = '-';
    do {
        tmp[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    } while (v && n < 15);
    while (n > 0 && len + 1 < cap) buf[len++] = tmp[--n];
    buf[len] = 0;
}

void appendHex(char* buf, size_t cap, size_t& len, uintptr_t val) {
    appendStr(buf, cap, len, "0x");
    char tmp[20];
    int n = 0;
    do {
        unsigned int d = static_cast<unsigned int>(val & 0xf);
        tmp[n++] = static_cast<char>(d < 10 ? ('0' + d) : ('a' + d - 10));
        val >>= 4;
    } while (val && n < 16);
    while (n > 0 && len + 1 < cap) buf[len++] = tmp[--n];
    buf[len] = 0;
}

void appendCodeAddr(char* buf, size_t cap, size_t& len, uintptr_t addr) {
    if (addr != 0 && g_ebootEnd > g_ebootBase && addr >= g_ebootBase && addr < g_ebootEnd) {
        appendStr(buf, cap, len, "eboot+");
        appendHex(buf, cap, len, addr - g_ebootBase);
        return;
    }
    if (addr != 0) {
        char modName[128];
        uintptr_t modBase = 0;
        if (platform::findModuleByAddr(addr, modName, sizeof(modName), modBase)) {
            appendStr(buf, cap, len, modName);
            appendStr(buf, cap, len, "+");
            appendHex(buf, cap, len, addr - modBase);
            return;
        }
    }
    appendHex(buf, cap, len, addr);
}

// Async-signal-safe: open / write / close only, no allocation.
void writeMarker(const char* kind, const char* crashLine = nullptr, uintptr_t rbp = 0, uintptr_t rsp = 0) {
    if (!g_marker[0]) return;
    int fd = ::open(g_marker, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    auto put = [fd](const char* s) { (void)!::write(fd, s, std::strlen(s)); };
    put(kind);
    put("\n");
    put(g_stage);
    put("\n");
    if (crashLine && crashLine[0]) {
        put(crashLine);
        // Optional caller return addresses via frame pointers, written after
        // the main crash line is already flushed to the descriptor.
        if (rsp != 0 && (rbp & (sizeof(uintptr_t) - 1)) == 0 && rbp >= rsp && rbp - rsp <= (256u << 10)) {
            uintptr_t fp = rbp;
            bool any = false;
            for (int depth = 0; depth < 6; ++depth) {
                const auto* frame = reinterpret_cast<const uintptr_t*>(fp);
                uintptr_t nextFp = frame[0];
                uintptr_t ret = frame[1];
                if (ret == 0) break;
                char item[96];
                size_t len = 0;
                appendStr(item, sizeof item, len, any ? "," : " bt=");
                appendCodeAddr(item, sizeof item, len, ret - 1);
                put(item);
                any = true;
                if ((nextFp & (sizeof(uintptr_t) - 1)) != 0 || nextFp <= fp || nextFp - fp > (64u << 10)) break;
                fp = nextFp;
            }
        }
        put("\n");
    }
    ::close(fd);
}

void onSignal(int sig, siginfo_t* info, void* uctx) {
    char kind[48];
    size_t klen = 0;
    appendStr(kind, sizeof kind, klen, "signal ");
    appendDec(kind, sizeof kind, klen, sig);
    appendStr(kind, sizeof kind, klen, " (");
    appendStr(kind, sizeof kind, klen, signalName(sig));
    appendStr(kind, sizeof kind, klen, ")");

    uintptr_t pc = 0, rbp = 0, rsp = 0, faultAddr = 0;
    platform::extractSignalContext(uctx, info, pc, rbp, rsp, faultAddr);

    char crashLine[192];
    size_t clen = 0;
    appendStr(crashLine, sizeof crashLine, clen, "crash: ");
    appendStr(crashLine, sizeof crashLine, clen, signalName(sig));
    appendStr(crashLine, sizeof crashLine, clen, " pc=");
    appendCodeAddr(crashLine, sizeof crashLine, clen, pc);
    appendStr(crashLine, sizeof crashLine, clen, " addr=");
    appendHex(crashLine, sizeof crashLine, clen, faultAddr);

    writeMarker(kind, crashLine, rbp, rsp);

    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    ::sigaction(sig, &dfl, nullptr);
    ::raise(sig);
}

}  // namespace

void install(const std::string& markerPath) {
    initBase();
    std::strncpy(g_marker, markerPath.c_str(), sizeof g_marker - 1);
    g_marker[sizeof g_marker - 1] = 0;
    struct sigaction sa{};
    sa.sa_sigaction = onSignal;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) ::sigaction(sig, &sa, nullptr);
    std::set_terminate([] {
        uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        char crashLine[160];
        size_t clen = 0;
        appendStr(crashLine, sizeof crashLine, clen, "crash: terminate pc=");
        appendCodeAddr(crashLine, sizeof crashLine, clen, caller ? caller - 1 : 0);
        appendStr(crashLine, sizeof crashLine, clen, " addr=0x0");
        if (auto ep = std::current_exception()) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                appendStr(crashLine, sizeof crashLine, clen, " what=");
                appendStr(crashLine, sizeof crashLine, clen, e.what());
            } catch (...) {
                appendStr(crashLine, sizeof crashLine, clen, " what=<non-std>");
            }
        }
        writeMarker("uncaught exception", crashLine);
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        ::sigaction(SIGABRT, &dfl, nullptr);
        std::abort();
    });
}

void setStage(const char* stage) {
    char tmp[sizeof g_stage];
    std::strncpy(tmp, stage, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    std::memcpy(g_stage, tmp, sizeof tmp);
}

void heartbeat() { g_beat = platform::nowMs(); }

void startWatchdog(uint32_t stallMs) {
    heartbeat();
    std::thread([stallMs] {
        bool stalled = false;
        uint64_t last = platform::nowMs();
        for (;;) {
            platform::sleepMs(1000);
            uint64_t now = platform::nowMs();
            if (now - last > 5000) {
                XC_LOGI("crash guard: the app was suspended for %llu ms (rest mode)",
                        static_cast<unsigned long long>(now - last));
                heartbeat();
                last = now;
                continue;
            }
            last = now;
            uint64_t beat = g_beat.load();
            uint64_t idle = now > beat ? now - beat : 0;
            if (!stalled && idle > stallMs) {
                stalled = true;
                XC_LOGW("crash guard: the main loop has not run for %llu ms (%s)", static_cast<unsigned long long>(idle), g_stage);
                writeMarker("stalled");
            } else if (stalled && idle < 1000) {
                stalled = false;
                XC_LOGI("crash guard: the main loop is back");
                ::unlink(g_marker);
            }
        }
    }).detach();
}

Previous takePrevious(const std::string& markerPath) {
    Previous p;
    std::string text;
    if (!platform::readFile(markerPath, text)) return p;
    ::unlink(markerPath.c_str());
    auto trimCr = [](std::string& s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    };
    size_t nl1 = text.find('\n');
    p.found = true;
    p.kind = text.substr(0, nl1);
    trimCr(p.kind);
    if (nl1 != std::string::npos) {
        size_t nl2 = text.find('\n', nl1 + 1);
        p.stage = text.substr(nl1 + 1, nl2 == std::string::npos ? std::string::npos : nl2 - (nl1 + 1));
        trimCr(p.stage);
        if (nl2 != std::string::npos) {
            p.crash = text.substr(nl2 + 1);
            trimCr(p.crash);
        }
    }
    return p;
}

}  // namespace xc::app::crash
