// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The Android platform layer (psbox): app data directory in internal storage,
// assets from package, POSIX threads and networking.
#include <cstdint>
#include "platform/platform.h"
#include "util/log.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#include <jni.h>
#include <dlfcn.h>

// Android Bionic libc introduced iconv in API 28 (Android 9.0).
// When targeting minSdk 26 (Android 8.0), prebuilt FFmpeg's subtitle decoder
// references iconv_open, iconv, and iconv_close. Providing weak fallback
// definitions satisfies the linker for API 26-27 without breaking API 28+.
extern "C" {
__attribute__((weak)) void* iconv_open(const char* tocode, const char* fromcode) {
    (void)tocode; (void)fromcode;
    return reinterpret_cast<void*>(-1);
}

__attribute__((weak)) size_t iconv(void* cd, char** inbuf, size_t* inbytesleft, char** outbuf, size_t* outbytesleft) {
    (void)cd; (void)inbuf; (void)inbytesleft; (void)outbuf; (void)outbytesleft;
    return static_cast<size_t>(-1);
}

__attribute__((weak)) int iconv_close(void* cd) {
    (void)cd;
    return 0;
}
}
#endif

namespace xc::platform {

namespace {
std::string g_dataDir;
std::string g_dataDirNote;

void makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            ::mkdir(path.substr(0, i).c_str(), 0700);
        }
    }
}
}  // namespace

bool init() {
    const char* env = std::getenv("XCLOUD_DATA_DIR");
    if (env && *env) {
        g_dataDir = env;
    } else {
        g_dataDir = "/data/data/com.rafaelngp.psbox/files";
    }
    makeDirs(g_dataDir);
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "PSBox", "platform_android init: dataDir=%s", g_dataDir.c_str());
#endif
    return true;
}

void shutdown() {}

std::string dataDir() { return g_dataDir; }
std::string dataDirNote() { return g_dataDirNote; }
bool dataDirWritable() { return true; }
int dataDirErrno() { return 0; }
std::string appDir() { return g_dataDir; }
std::string assetDir() {
    const char* env = std::getenv("XCLOUD_ASSETS");
    if (env && *env) return env;
    return g_dataDir + "/assets";
}
std::string caBundlePath() {
    const char* env = std::getenv("XCLOUD_CA_BUNDLE");
    if (env && *env) return env;
    return assetDir() + "/cacert.pem";
}

bool randomBytes(void* out, size_t len) {
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    auto* p = static_cast<unsigned char*>(out);
    while (len > 0) {
        ssize_t n = ::read(fd, p, len);
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    ::close(fd);
    return true;
}

bool resolveIPv4(const std::string& host, std::vector<uint32_t>& addrs, std::string& err) {
    in_addr literal{};
    if (::inet_pton(AF_INET, host.c_str(), &literal) == 1) {
        addrs.push_back(literal.s_addr);
        return true;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || !res) {
        err = "DNS lookup failed for " + host + ": " + gai_strerror(rc);
        return false;
    }
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        if (p->ai_family == AF_INET && p->ai_addr) {
            uint32_t a = reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr.s_addr;
            if (std::find(addrs.begin(), addrs.end(), a) == addrs.end()) {
                addrs.push_back(a);
            }
        }
    }
    ::freeaddrinfo(res);
    if (addrs.empty()) {
        err = "DNS lookup failed for " + host + ": no IPv4 address found";
        return false;
    }
    return true;
}

bool resolveIPv4(const std::string& host, uint32_t& addr, std::string& err) {
    std::vector<uint32_t> addrs;
    if (!resolveIPv4(host, addrs, err)) return false;
    addr = addrs[0];
    return true;
}

void probeNetworking() {}

void notify(const std::string& text) { notify(text, text); }

void notify(const std::string& text, const std::string& forLog) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "PSBox", "%s", text.c_str());
#else
    std::printf("[Android Notify] %s (%s)\n", text.c_str(), forLog.c_str());
#endif
}

void makePrivate(const std::string& path) { ::chmod(path.c_str(), 0600); }

bool systemKeyboardAvailable() { return false; }
bool openSystemKeyboard(const std::string&, const std::string&, size_t, KeyboardKind) { return false; }
KeyboardStatus pollSystemKeyboard(std::string&) { return KeyboardStatus::Closed; }

bool restartApp() { return false; }

std::string firmwareVersion() { return {}; }
std::string diagnostics() { return {}; }
std::string platformName() { return "Android"; }
bool isPS4() { return false; }
bool isPS5() { return false; }
size_t imageCacheBudget() { return 96u << 20; }

void setNoThreadPinning(bool) {}
bool isHandheld() { return false; }
void forceHandheld(bool) {}

namespace {
void setAndroidThreadPriority(int nicePrio) {
#if defined(__ANDROID__)
    typedef void* (*PFN_GetJNIEnv)();
    static auto pfnGetJNIEnv = reinterpret_cast<PFN_GetJNIEnv>(dlsym(RTLD_DEFAULT, "SDL_GetAndroidJNIEnv"));
    if (pfnGetJNIEnv) {
        if (auto* env = static_cast<JNIEnv*>(pfnGetJNIEnv())) {
            jclass procCls = env->FindClass("android/os/Process");
            if (procCls) {
                jmethodID mid = env->GetStaticMethodID(procCls, "setThreadPriority", "(I)V");
                if (mid) {
                    env->CallStaticVoidMethod(procCls, mid, static_cast<jint>(nicePrio));
                }
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(procCls);
            }
        }
    }
    // Fallback or reinforce with direct POSIX setpriority
    int rc = ::setpriority(PRIO_PROCESS, 0, nicePrio);
    if (rc != 0 && nicePrio < 0) {
        ::setpriority(PRIO_PROCESS, 0, std::max(nicePrio, -8));
    }
#else
    (void)nicePrio;
#endif
}
}  // namespace

void setThreadAffinity(ThreadRole role, const char* name) {
#if defined(__ANDROID__)
    if (name && *name) {
        char threadName[16]{};
        std::strncpy(threadName, name, 15);
        pthread_setname_np(pthread_self(), threadName);
    }
    switch (role) {
    case ThreadRole::Audio:
        setAndroidThreadPriority(-19); // THREAD_PRIORITY_URGENT_AUDIO
        break;
    case ThreadRole::VideoDecode:
        setAndroidThreadPriority(-8);  // THREAD_PRIORITY_URGENT_DISPLAY
        break;
    case ThreadRole::Network:
        setAndroidThreadPriority(-4);  // THREAD_PRIORITY_DISPLAY
        break;
    case ThreadRole::Main:
        setAndroidThreadPriority(-10); // Top-app boost
        break;
    case ThreadRole::Worker:
        setAndroidThreadPriority(0);   // THREAD_PRIORITY_DEFAULT
        break;
    }
#else
    (void)role; (void)name;
#endif
}

void setThreadRealtimePriority() {
#if defined(__ANDROID__)
    setAndroidThreadPriority(-19);
    struct sched_param param{};
    param.sched_priority = 1;
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);
#endif
}

void setLowLatencyNetwork(bool on) {
#if defined(__ANDROID__)
    typedef void* (*PFN_GetJNIEnv)();
    static auto pfnGetJNIEnv = reinterpret_cast<PFN_GetJNIEnv>(dlsym(RTLD_DEFAULT, "SDL_GetAndroidJNIEnv"));
    if (!pfnGetJNIEnv) return;
    auto* env = static_cast<JNIEnv*>(pfnGetJNIEnv());
    if (!env) return;
    jclass cls = env->FindClass("com/rafaelngp/psbox/MainActivity");
    if (!cls) {
        env->ExceptionClear();
        return;
    }
    jmethodID mid = env->GetStaticMethodID(cls, "setLowLatencyWifi", "(Z)V");
    if (mid) {
        env->CallStaticVoidMethod(cls, mid, static_cast<jboolean>(on));
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
#else
    (void)on;
#endif
}
void getCodeBounds(uintptr_t& base, uintptr_t& end) { base = 0; end = 0; }
bool findModuleByAddr(uintptr_t, char*, size_t, uintptr_t&) { return false; }
void extractSignalContext(void*, void*, uintptr_t& pc, uintptr_t& rbp, uintptr_t& rsp, uintptr_t& faultAddr) {
    pc = 0; rbp = 0; rsp = 0; faultAddr = 0;
}

}  // namespace xc::platform
