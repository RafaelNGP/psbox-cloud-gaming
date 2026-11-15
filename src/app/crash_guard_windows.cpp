// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/crash_guard.h"

#include "platform/platform.h"
#include "util/log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <thread>

namespace xc::app::crash {

namespace {

char g_marker[512] = {};
char g_stage[96] = "starting";
std::atomic<uint64_t> g_beat{0};
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

void appendStr(char* buf, size_t cap, size_t& len, const char* s) {
    if (!s) return;
    while (*s && len + 1 < cap) buf[len++] = *s++;
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

void writeMarker(const char* kind, const char* crashLine = nullptr) {
    if (!g_marker[0]) return;
    HANDLE hFile = CreateFileA(g_marker, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    auto put = [hFile](const char* s) {
        if (!s) return;
        DWORD written = 0;
        WriteFile(hFile, s, static_cast<DWORD>(std::strlen(s)), &written, nullptr);
    };
    put(kind);
    put("\n");
    put(g_stage);
    put("\n");
    if (crashLine && crashLine[0]) {
        put(crashLine);
        put("\n");
    }
    CloseHandle(hFile);
}

const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    default: return "EXCEPTION";
    }
}

LONG WINAPI topLevelFilter(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    // Ignore non-fatal / debug exceptions
    if (code == 0xE06D7363 /* C++ exception */ || code == DBG_PRINTEXCEPTION_C) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    char kind[64];
    size_t klen = 0;
    appendStr(kind, sizeof kind, klen, "signal 11 (");
    appendStr(kind, sizeof kind, klen, exceptionName(code));
    appendStr(kind, sizeof kind, klen, ")");

    uintptr_t pc = 0;
    uintptr_t faultAddr = 0;
    if (ep->ContextRecord) {
#if defined(_M_X64) || defined(__x86_64__)
        pc = ep->ContextRecord->Rip;
#elif defined(_M_IX86) || defined(__i386__)
        pc = ep->ContextRecord->Eip;
#endif
    }
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        faultAddr = ep->ExceptionRecord->ExceptionInformation[1];
    }

    HMODULE hMod = GetModuleHandleA(nullptr);
    uintptr_t base = reinterpret_cast<uintptr_t>(hMod);

    char crashLine[192];
    size_t clen = 0;
    appendStr(crashLine, sizeof crashLine, clen, "crash: ");
    appendStr(crashLine, sizeof crashLine, clen, exceptionName(code));
    appendStr(crashLine, sizeof crashLine, clen, " pc=psbox+");
    if (pc >= base) {
        appendHex(crashLine, sizeof crashLine, clen, pc - base);
    } else {
        appendHex(crashLine, sizeof crashLine, clen, pc);
    }
    appendStr(crashLine, sizeof crashLine, clen, " addr=");
    appendHex(crashLine, sizeof crashLine, clen, faultAddr);

    writeMarker(kind, crashLine);

    if (g_prevFilter) return g_prevFilter(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void install(const std::string& markerPath) {
    std::strncpy(g_marker, markerPath.c_str(), sizeof g_marker - 1);
    g_marker[sizeof g_marker - 1] = 0;

    g_prevFilter = SetUnhandledExceptionFilter(topLevelFilter);

    std::set_terminate([] {
        char crashLine[160];
        size_t clen = 0;
        appendStr(crashLine, sizeof crashLine, clen, "crash: terminate pc=psbox+0x0 addr=0x0");
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
                DeleteFileA(g_marker);
            }
        }
    }).detach();
}

Previous takePrevious(const std::string& markerPath) {
    Previous p;
    std::string text;
    if (!platform::readFile(markerPath, text)) return p;
    DeleteFileA(markerPath.c_str());
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
