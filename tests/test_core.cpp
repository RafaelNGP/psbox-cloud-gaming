// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "test_harness.h"

#include "app/auto_deband.h"
#include "app/crash_guard.h"
#include "app/gyro_calibration.h"
#include "app/library.h"
#include "app/report.h"
#include "app/settings.h"
#include "app/settings_manager.h"
#include "app/shell.h"
#include "app/stream_player.h"
#include "app/update_check.h"
#include "app/updater.h"
#include "app/web_server.h"
#include "auth/account_seal.h"
#include "auth/auth_manager.h"
#include "auth/msa.h"
#include "auth/xbox_live.h"
#include "display/display.h"
#include "display/gpu.h"
#include "input/controller.h"
#include "input/gyro.h"
#include "input/kbm.h"
#include "input/keyboard_nav.h"
#include "input/pad_map.h"
#include "input/pad_usb.h"
#include "input/pad_usb_hid.h"
#include "input/sdl_mapping.h"
#include "input/tuning.h"
#include "input/usb_xray.h"
#include "media/audio_encoder.h"
#include "media/audio_in.h"
#include "media/audio_in_common.h"
#include "media/audio_out.h"
#include "media/audio_queue.h"
#include "media/decoder.h"
#include "media/hw_decoder.h"
#include "net/http.h"
#include "net/socket.h"
#include "platform/handheld.h"
#include "platform/platform.h"
#include "platform/toasts.h"
#include "stream/h264_receiver.h"
#include "stream/input_packet.h"
#include "stream/stream_session.h"
#include "trophy/trophy_backend.h"
#include "trophy/trophy_registry.h"
#include "trophy/trophy_sync.h"
#include "ui/accent_color.h"
#include "ui/app_ui.h"
#include "ui/app_ui_internal.h"
#include "ui/cloud_time.h"
#include "ui/image_cache.h"
#include "ui/input_mode.h"
#include "ui/pad_glyphs.h"
#include "ui/stream_menu.h"
#include "ui/strings.h"
#include "ui/touch_pad_overlay.h"
#include "util/fs.h"
#include "util/json.h"
#include "util/log.h"
#include "xcloud/achievements.h"
#include "xcloud/catalog.h"
#include "xcloud/gssv.h"
#include "xcloud/prices.h"
#include "xcloud/purchase.h"
#include "xcloud/regions.h"
#include "xcloud/titlehub.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace xc;

static void testJson() {
    auto v = json::parse(R"({"a":1,"b":[true,null,"x\u00e9\n"],"c":{"d":-2.5e1},"e":"\ud83c\udfae"})");
    CHECK(v.has_value());
    CHECK((*v)["a"].asInt() == 1);
    CHECK((*v)["b"].size() == 3);
    CHECK((*v)["b"][0].asBool());
    CHECK((*v)["b"][1].isNull());
    CHECK((*v)["b"][2].str() == "x\xc3\xa9\n");
    CHECK((*v)["c"]["d"].asNumber() == -25.0);
    CHECK((*v)["e"].str() == "\xf0\x9f\x8e\xae");
    CHECK((*v)["missing"]["deep"].str("def") == "def");

    // exchangeResponse-style nested JSON string round trip.
    json::Value inner = json::Value::object();
    inner.set("sdp", "v=0\r\no=- 1 2 IN IP4 0.0.0.0\r\n");
    json::Value outer = json::Value::object();
    outer.set("exchangeResponse", inner.dump());
    auto back = json::parse(outer.dump());
    CHECK(back.has_value());
    auto innerBack = json::parse((*back)["exchangeResponse"].str());
    CHECK(innerBack && (*innerBack)["sdp"].str() == "v=0\r\no=- 1 2 IN IP4 0.0.0.0\r\n");

    CHECK(!json::parse("{\"a\":}").has_value());
    CHECK(!json::parse("[1,2").has_value());
    CHECK(!json::parse("{} x").has_value());
    CHECK(json::Value(int64_t(2148916233)).dump() == "2148916233");

    // Copy-on-write: mutating a copy must not alter the original.
    json::Value a = json::Value::array();
    a.push(1);
    json::Value b = a;
    b.push(2);
    CHECK(a.size() == 1 && b.size() == 2);
}

static void testUrl() {
    net::Url u;
    CHECK(net::Url::parse("https://xgpuweb.gssv-play-prod.xboxlive.com/v2/login/user", u));
    CHECK(u.host == "xgpuweb.gssv-play-prod.xboxlive.com" && u.port == 443 && u.path == "/v2/login/user");
    CHECK(net::Url::parse("https://example.com:8443", u));
    CHECK(u.port == 8443 && u.path == "/");
    CHECK(!net::Url::parse("ftp://x", u));
    CHECK(net::urlEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
    CHECK(net::formEncode({{"grant_type", "refresh_token"}, {"scope", "service::x::MBI_SSL"}}) ==
          "grant_type=refresh_token&scope=service%3A%3Ax%3A%3AMBI_SSL");
}

static void testBuiltinCa() {
    // A missing bundle (an incomplete copy of the app): the copy built in.
    CHECK(xc::net::initTls("/nonexistent/cacert.pem"));
    CHECK(xc::net::usingBuiltinCa());
    xc::net::shutdownTls();
}

static void testHttpKeepAlive() {
    using net::chunkedLength;
    using net::keepAliveAfter;
    std::string whole = "4\r\nWiki\r\n5;x=1\r\npedia\r\n0\r\n\r\n";
    CHECK(chunkedLength(whole) == static_cast<long long>(whole.size()));
    for (size_t n = 0; n < whole.size(); ++n) CHECK(chunkedLength(whole.substr(0, n)) == 0);  // more to come
    std::string trailers = "3\r\nabc\r\n0\r\nX-Trailer: 1\r\n\r\n";
    CHECK(chunkedLength(trailers) == static_cast<long long>(trailers.size()));
    CHECK(chunkedLength(trailers.substr(0, trailers.size() - 2)) == 0);
    // Data that happens to end like a last chunk is not the end.
    std::string tricky = "5\r\nab0\r\n\r\n";
    CHECK(chunkedLength(tricky) == 0);
    CHECK(chunkedLength(tricky + "0\r\n\r\n") == static_cast<long long>(tricky.size() + 5));
    // Bytes after the end: the length stops at the end, so the caller sees them.
    CHECK(chunkedLength(whole + "HTTP/1.1 200 OK") == static_cast<long long>(whole.size()));
    CHECK(chunkedLength("zz\r\nab\r\n0\r\n\r\n") == -1);
    CHECK(chunkedLength("2\r\nabXX0\r\n\r\n") == -1);
    CHECK(chunkedLength("ffffffffffffffff\r\nab\r\n") == -1);
    CHECK(chunkedLength(std::string(2000, '1')) == -1);  // a size line that never ends

    CHECK(keepAliveAfter(200, false, "", 10, 10));
    CHECK(keepAliveAfter(404, false, "keep-alive", 0, 0));
    CHECK(!keepAliveAfter(200, false, "close", 10, 10));
    CHECK(!keepAliveAfter(200, false, "Close", 10, 10));
    CHECK(!keepAliveAfter(200, false, "", 10, 12));   // bytes past the body
    CHECK(!keepAliveAfter(200, false, "", 10, 8));    // body cut short
    CHECK(!keepAliveAfter(200, false, "", -1, 10));   // ended by the server closing
    CHECK(!keepAliveAfter(0, false, "", 0, 0));       // no answer
    CHECK(!keepAliveAfter(200, true, "", 10, 10));    // HTTP/1.0 closes by default
    CHECK(keepAliveAfter(200, true, "Keep-Alive", 10, 10));
}

static void testResolveIPv4AndConnectTcp() {
    std::string err;
    std::vector<uint32_t> addrs;
    CHECK(platform::resolveIPv4("127.0.0.1", addrs, err));
    CHECK(!addrs.empty());
    CHECK(addrs[0] == htonl(0x7F000001));

    uint32_t singleAddr = 0;
    CHECK(platform::resolveIPv4("127.0.0.1", singleAddr, err));
    CHECK(singleAddr == htonl(0x7F000001));

    addrs.clear();
    CHECK(!platform::resolveIPv4("this-domain-should-never-exist-xyz123.local", addrs, err));
    CHECK(addrs.empty());
    CHECK(!err.empty());

    // Connecting to an unopened port on loopback fails with connection refusal, NOT a DNS error
    int fd = net::connectTcp("127.0.0.1", 1, 500, err);
    CHECK(fd < 0);
    CHECK(err.find("DNS lookup failed") == std::string::npos);
    CHECK(err.find("connect failed to 127.0.0.1:1") != std::string::npos ||
          err.find("connect timed out") != std::string::npos);

    // Sinkhole addresses are detected
    fd = net::connectTcp("0.0.0.0", 443, 500, err);
    CHECK(fd < 0);
    CHECK(err.find("sinkhole IP") != std::string::npos);

    // Successful connectTcp to a listening server socket
    net::Socket server = net::createTcpSocket();
    if (server.valid()) {
        if (net::bindSocket(server, "127.0.0.1", 0) &&
            net::listenSocket(server, 1)) {
            int port = 0;
            if (net::getSocketPort(server, port) && port > 0) {
                int clientFd = net::connectTcp("127.0.0.1", port, 2000, err);
                CHECK(clientFd >= 0);
                if (clientFd >= 0) {
                    net::Socket client{clientFd};
                    net::closeSocket(client);
                }
            }
        }
        net::closeSocket(server);
    }
}

// Verifies xc::net::Socket: creation, non-blocking configuration, timeouts,
// loopback connect/accept/send/recv, and clean closure.

static void testSocketLayer() {
    // 1. Sockets initialization
    CHECK(net::socketsInit());

    // 2. Invalid socket invariant
    net::Socket invalidSock;
    CHECK(!invalidSock.valid());
    CHECK(invalidSock.fd == -1);
    net::closeSocket(invalidSock); // must safely no-op without crashing or leaking
    CHECK(!invalidSock.valid());

    // 3. Socket creation
    net::Socket listenSock = net::createTcpSocket();
    CHECK(listenSock.valid());

    // 4. Non-blocking configuration
    CHECK(net::setNonBlocking(listenSock, true));

    // 5. Timeout configuration (receive and send)
    CHECK(net::setRecvTimeout(listenSock, 1000));
    CHECK(net::setSendTimeout(listenSock, 1000));

    // 6. Loopback binding to ephemeral port (port 0)
    int port = 0;
    CHECK(net::bindSocket(listenSock, "127.0.0.1", 0));
    CHECK(net::getSocketPort(listenSock, port));
    CHECK(port > 0);
    CHECK(net::listenSocket(listenSock, 1));

    // 7. Loopback client connection
    net::Socket clientSock = net::createTcpSocket();
    CHECK(clientSock.valid());
    CHECK(net::setNonBlocking(clientSock, true));

    // In non-blocking mode, connectSocket initiates the handshake
    (void)net::connectSocket(clientSock, "127.0.0.1", port);

    // Wait for connection to establish using pollSockets (write event on client)
    net::Socket writeSocks[1] = {clientSock};
    int pr = net::pollSockets(nullptr, 0, writeSocks, 1, 2000);
    CHECK(pr > 0);

    // 8. Server accept
    net::Socket readListen[1] = {listenSock};
    pr = net::pollSockets(readListen, 1, nullptr, 0, 2000);
    CHECK(pr > 0);

    net::Socket acceptedSock = net::acceptSocket(listenSock);
    CHECK(acceptedSock.valid());
    CHECK(net::setNonBlocking(acceptedSock, true));

    // 9. Send and Receive verification over loopback
    const char kPayload[] = "PSBox_Socket_Abstraction_M2_Test_Payload";
    const size_t kLen = sizeof(kPayload);
    ssize_t sent = net::sendNoSignal(clientSock, kPayload, kLen);
    CHECK(sent == static_cast<ssize_t>(kLen));

    // Poll accepted socket for read availability
    net::Socket readAcc[1] = {acceptedSock};
    pr = net::pollSockets(readAcc, 1, nullptr, 0, 2000);
    CHECK(pr > 0);

    char recvBuf[64] = {0};
    ssize_t recvd = net::recvSocket(acceptedSock, recvBuf, sizeof(recvBuf));
    CHECK(recvd == static_cast<ssize_t>(kLen));
    CHECK(std::strcmp(recvBuf, kPayload) == 0);

    // 10. Clean closure
    net::closeSocket(clientSock);
    CHECK(!clientSock.valid());
    net::closeSocket(acceptedSock);
    CHECK(!acceptedSock.valid());
    net::closeSocket(listenSock);
    CHECK(!listenSock.valid());

    // 11. Read timeout on non-connected socket
    net::Socket timeoutSock = net::createTcpSocket();
    CHECK(timeoutSock.valid());
    CHECK(net::setRecvTimeout(timeoutSock, 50));
    char dummy[16];
    ssize_t r = net::recvSocket(timeoutSock, dummy, sizeof(dummy));
    CHECK(r <= 0); // must return error or would block
    net::closeSocket(timeoutSock);
    CHECK(!timeoutSock.valid());

    // 12. Address parsing verification
    uint8_t ipv6Bytes[16] = {0};
    CHECK(net::parseIpv6Address("2001:0db8:85a3:0000:0000:8a2e:0370:7334", ipv6Bytes));
    CHECK(ipv6Bytes[0] == 0x20 && ipv6Bytes[1] == 0x01);
    CHECK(!net::parseIpv6Address("invalid:ipv6::address::extra", ipv6Bytes));
    uint8_t ipv4Bytes[4] = {0};
    CHECK(net::parseIpv4Address("127.0.0.1", ipv4Bytes));
    CHECK(ipv4Bytes[0] == 127 && ipv4Bytes[1] == 0 && ipv4Bytes[2] == 0 && ipv4Bytes[3] == 1);
}

// Verifies xc::fs: makeDirs (nested), exists, isDir, file creation, atomic renameReplace,
// removeFile, removeTree, listDir, listTree, and UTF-8 path handling.

static void testFsLayer() {
    const std::string tmpBase = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
    const std::string testRoot = (!platform::dataDir().empty() ? platform::dataDir() : tmpBase) + "/test_psbox_fs_m2";

    // Ensure clean starting state
    xc::fs::removeTree(testRoot);
    CHECK(!xc::fs::exists(testRoot));

    // 1. Nested makeDirs
    const std::string nestedDir = testRoot + "/nested/level1/level2/level3";
    CHECK(xc::fs::makeDirs(nestedDir));
    CHECK(xc::fs::exists(nestedDir));
    CHECK(xc::fs::isDir(nestedDir));
    CHECK(!xc::fs::isDir(testRoot + "/nonexistent_folder"));
    CHECK(xc::fs::makeDirs(nestedDir)); // Idempotency check: returns true if directory exists

    // 2. File creation, exists, and fileSize
    const std::string fileA = nestedDir + "/fileA.txt";
    const std::string contentA = "PSBox Filesystem Abstraction Test Content A";
    FILE* fp = std::fopen(fileA.c_str(), "wb");
    CHECK(fp != nullptr);
    if (fp) {
        std::fwrite(contentA.data(), 1, contentA.size(), fp);
        std::fclose(fp);
    }
    CHECK(xc::fs::exists(fileA));
    CHECK(!xc::fs::isDir(fileA));
    uint64_t szA = 0;
    CHECK(xc::fs::fileSize(fileA, szA));
    CHECK(szA == contentA.size());

    // 3. Atomic renameReplace over existing file
    const std::string fileB = nestedDir + "/fileB.txt";
    const std::string contentB = "Old Content B to be atomically replaced";
    fp = std::fopen(fileB.c_str(), "wb");
    CHECK(fp != nullptr);
    if (fp) {
        std::fwrite(contentB.data(), 1, contentB.size(), fp);
        std::fclose(fp);
    }
    CHECK(xc::fs::exists(fileB));

    // renameReplace: atomically replaces fileB with fileA
    CHECK(xc::fs::renameReplace(fileA, fileB));
    CHECK(!xc::fs::exists(fileA));
    CHECK(xc::fs::exists(fileB));
    uint64_t szB = 0;
    CHECK(xc::fs::fileSize(fileB, szB));
    CHECK(szB == contentA.size());

    // 4. Directory listing: listDir
    const std::string fileC = nestedDir + "/fileC.dat";
    fp = std::fopen(fileC.c_str(), "wb");
    if (fp) {
        std::fputc(0x5A, fp);
        std::fclose(fp);
    }
    auto dirEntries = xc::fs::listDir(nestedDir);
    CHECK(dirEntries.size() == 2); // fileB.txt and fileC.dat
    bool foundB = false, foundC = false, foundDots = false;
    for (const auto& name : dirEntries) {
        if (name == "fileB.txt") foundB = true;
        if (name == "fileC.dat") foundC = true;
        if (name == "." || name == "..") foundDots = true;
    }
    CHECK(foundB);
    CHECK(foundC);
    CHECK(!foundDots); // listDir MUST exclude "." and ".."

    // 5. Recursive directory listing: listTree
    auto treeEntries = xc::fs::listTree(testRoot);
    CHECK(treeEntries.size() == 2);
    bool treeFoundB = false, treeFoundC = false;
    for (const auto& rel : treeEntries) {
        if (rel.find("fileB.txt") != std::string::npos) treeFoundB = true;
        if (rel.find("fileC.dat") != std::string::npos) treeFoundC = true;
    }
    CHECK(treeFoundB);
    CHECK(treeFoundC);

    // 6. UTF-8 path handling
    const std::string utf8Dir = testRoot + "/pasta_com_acentuação_🎮";
    CHECK(xc::fs::makeDirs(utf8Dir));
    CHECK(xc::fs::exists(utf8Dir));
    CHECK(xc::fs::isDir(utf8Dir));

    const std::string utf8File = utf8Dir + "/título_configuração_日本語.json";
#if defined(_WIN32)
    CHECK(xc::fs::writeFile(utf8File, "{\"status\":\"ok\"}"));
#else
    fp = std::fopen(utf8File.c_str(), "wb");
    CHECK(fp != nullptr);
    if (fp) {
        const char kJson[] = "{\"status\":\"ok\"}";
        std::fwrite(kJson, 1, std::strlen(kJson), fp);
        std::fclose(fp);
    }
#endif
    CHECK(xc::fs::exists(utf8File));
    auto utf8Entries = xc::fs::listDir(utf8Dir);
    CHECK(utf8Entries.size() == 1);
    if (!utf8Entries.empty()) {
        CHECK(utf8Entries[0] == "título_configuração_日本語.json");
    }

    // 7. removeFile (including idempotency on non-existent files)
    CHECK(xc::fs::removeFile(utf8File));
    CHECK(!xc::fs::exists(utf8File));
    CHECK(xc::fs::removeFile(utf8File)); // idempotent: removing non-existent file returns true

    // 8. writeFileAtomic, readFileString, makeWorldWritable, parentPath, modifiedTime
    const std::string atomicFile = testRoot + "/atomic.txt";
    CHECK(xc::fs::writeFileAtomic(atomicFile, "Atomic Content 123"));
    CHECK(xc::fs::exists(atomicFile));
    std::string readBack;
    CHECK(xc::fs::readFileString(atomicFile, readBack));
    CHECK(readBack == "Atomic Content 123");
    CHECK(xc::fs::makeWorldWritable(atomicFile));
    int64_t mtime = 0;
    CHECK(xc::fs::modifiedTime(atomicFile, mtime));
    CHECK(mtime > 0);
    CHECK(xc::fs::parentPath(atomicFile) == testRoot);

    // 9. removeTree
    CHECK(xc::fs::removeTree(testRoot));
    CHECK(!xc::fs::exists(testRoot));
    CHECK(!xc::fs::exists(nestedDir));
    CHECK(xc::fs::removeTree(testRoot)); // idempotent: removing non-existent directory returns true
}

// The home rows come from the network on every load(): the cache's rows
// (kept for the instant start) are never added to them, and a cache that
// holds every row twice heals at the next save.

static void testSteamDeckDmi() {
    using xc::platform::steamDeckDmi;
    CHECK(steamDeckDmi("Valve\n", "Jupiter\n"));
    CHECK(steamDeckDmi("Valve", "Galileo"));
    CHECK(!steamDeckDmi("Valve\n", "Index\n"));
    CHECK(!steamDeckDmi("ASUSTeK COMPUTER INC.\n", "ROG Ally RC71L_RC71L\n"));
    CHECK(!steamDeckDmi("", ""));
}

static void testNullBackends() {
    CHECK(input::init());
    input::ControllerState pad;
    pad.btnA = true;
    CHECK(!input::poll(pad) && !pad.connected && !pad.btnA);
    CHECK(!input::pollPad(2, pad) && !input::padConnected(0) && input::padUserName(0).empty());
    CHECK(input::padUserId(0) == -1 && input::padActiveAt(0) == 0);
    input::setCircleConfirms(true);
    CHECK(input::circleConfirms());
    input::setCircleConfirms(false);
    CHECK(!input::circleConfirms());
    CHECK(!input::kbmInit(-1));
    input::KbmInput kbm = input::kbmPoll();
    CHECK(!kbm.keyboard && !kbm.mouse && kbm.keys.empty() && !kbm.moved);
    media::HwDecoder hw;
    CHECK(!hw.init(1920, 1088) && !hw.ready());
    display::DisplayCaps caps = display::caps();
    CHECK(!caps.upscalers && !caps.deband && !caps.sharpen && !caps.highRefreshSwitch && !caps.presentTiming);
    CHECK(!display::canSwitchRefresh() && !display::setHighRefresh(true) && !display::bringUpTest(1));
}

static void testNullBackendsM1() {
    input::PadCaps caps = input::padCaps(0), any = input::anyPadCaps();
    CHECK(caps.kind == input::PadKind::None && !caps.rumble && !caps.triggerRumble && !caps.adaptiveTriggers);
    CHECK(any.kind == input::PadKind::None && !any.lightBar && !any.gyro && !any.touchpad);
    CHECK(input::padDeviceName(0).empty() && input::padCaps(9).kind == input::PadKind::None);
    input::setRumble(255, 255, 100, 0);  // nowhere to go, and nothing breaks
    input::setLightBar(1, 2, 3, 0);
    input::ControllerState pad;
    CHECK(!input::poll(pad) && !input::padConnected(0));
    input::kbmSetCaptured(true);
    input::KbmInput kbm = input::kbmPoll();
    CHECK(!kbm.keyboard && !kbm.mouse && !kbm.moved && kbm.dx == 0);
    input::kbmSetCaptured(false);
    CHECK(media::audioStart() && media::audioStart());
    std::vector<float> sound(48 * 2 * 260, 0.1f);  // 260 ms, never played: kept to the bound
    media::audioPush(sound.data(), 48 * 260);
    CHECK(media::audioQueue().queuedFrames() == media::AudioQueue::kTrimTo);
    media::audioStop();
    CHECK(media::audioQueue().queuedFrames() == 0);
    media::audioPush(sound.data(), 480);
    CHECK(media::audioQueue().queuedFrames() == 0);  // stopped: ignored
    media::AudioIn mic;
    bool called = false;
    CHECK(mic.open([&](const int16_t*, size_t) { called = true; }) && mic.isRecording());
    CHECK(!mic.isHardwareMuted() && mic.level() == 0.0f);
    mic.close();
    CHECK(!mic.isRecording() && !called);
}

static void testToasts() {
    using platform::ToastQueue;
    ToastQueue q;
    uint64_t v0 = q.version(1000);
    CHECK(q.visible(1000).empty() && q.version(1000) == v0);
    q.push("one", 1000);
    q.push("", 1000);  // nothing
    CHECK(q.visible(1000) == std::vector<std::string>{"one"});
    uint64_t v1 = q.version(1000);
    CHECK(v1 != v0 && q.version(1001) == v1);  // nothing changed: no redraw
    q.push("two", 2000);
    q.push("three", 2500);
    q.push("four", 3000);  // waits: three on screen at most
    CHECK((q.visible(3000) == std::vector<std::string>{"one", "two", "three"}));
    // "one" has had its time: "four" takes its place, and the version moves.
    uint64_t v2 = q.version(5999);
    CHECK(q.version(6000) != v2);
    CHECK((q.visible(6000) == std::vector<std::string>{"two", "three", "four"}));
    // The same text again restarts it at the end instead of stacking.
    q.push("two", 6500);
    CHECK((q.visible(6500) == std::vector<std::string>{"three", "four", "two"}));
    CHECK((q.visible(7499) == std::vector<std::string>{"three", "four", "two"}));
    CHECK((q.visible(7500) == std::vector<std::string>{"four", "two"}));  // three's time (from 2500) is up
    CHECK((q.visible(11000) == std::vector<std::string>{"two"}));        // four's (from 6000)
    CHECK(q.visible(11500).empty());
    // Bounded: the oldest dropped beyond kMaxQueued.
    for (size_t i = 0; i < ToastQueue::kMaxQueued + 5; ++i) q.push("n" + std::to_string(i), 20000);
    auto shown = q.visible(20000);
    CHECK(shown.size() == ToastQueue::kMaxShown && shown.front() == "n5");
    q.clear();
    CHECK(q.visible(20000).empty());
    // Off by default: the tools keep printing notifications.
    CHECK(!platform::toastsEnabled());
}

static void testTrophyNonConsoleDisabled() {
    using namespace xc;
    // 1. Trophy stub functions are safe no-ops
    trophy::startTrophyThread();
    CHECK(trophy::stopTrophyThread(100));
    trophy::resumeTrophySync();
    trophy::requestTrophySync(true);
    trophy::cancelTrophySync();
    trophy::requestRemoveTrophies();
    CHECK(!trophy::dispatchStreamPoll("auth", "xuid", "title", "name"));
    CHECK(!trophy::isSyncing());
    CHECK(trophy::titlesDone() == 0);
    CHECK(trophy::titlesTotal() == 0);
    trophy::setTrophyContextProvider({});
    trophy::setTrophyUserProvider({});
    trophy::runPsboxNativeSelfTest();
    CHECK(trophy::normalizeTrophyName("Test Trophy") == "Test Trophy");
    CHECK(trophy::findKnownTitle("12345") == nullptr);

    // 2. UiCaps trophies visibility verification
    ui::UiCaps capsOn;
    capsOn.trophies = true;
    auto rowsOn = ui::visibleSettingsRows(capsOn);
    bool found24On = false, found25On = false;
    for (const auto& r : rowsOn) {
        if (r.row == 24) found24On = true;
        if (r.row == 25) found25On = true;
    }
    CHECK(found24On);
    CHECK(found25On);

    ui::UiCaps capsOff;
    capsOff.trophies = false;
    auto rowsOff = ui::visibleSettingsRows(capsOff);
    bool found24Off = false, found25Off = false;
    for (const auto& r : rowsOff) {
        if (r.row == 24) found24Off = true;
        if (r.row == 25) found25Off = true;
    }
    CHECK(!found24Off);
    CHECK(!found25Off);
}

#if !defined(_WIN32)
[[gnu::noinline]] static void triggerNullWriteCrash() {
    volatile int* p = nullptr;
    asm volatile("" : "+r"(p));
    *p = 1;
}

static void testCrashGuard() {
    // A crash in a child process leaves the marker with its stage and faulting PC offset.
    std::string marker = "/tmp/xcloud-tests-crash.marker";
    std::remove(marker.c_str());
    pid_t pid = fork();
    if (pid == 0) {
        xc::app::crash::install(marker);
        xc::app::crash::setStage("streaming TEST");
        triggerNullWriteCrash();
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);  // still dies of it
    auto prev = xc::app::crash::takePrevious(marker);
    CHECK(prev.found && prev.kind == "signal 11 (SIGSEGV)" && prev.stage == "streaming TEST");
    CHECK(prev.crash.rfind("crash: SIGSEGV pc=eboot+0x", 0) == 0);
    CHECK(prev.crash.find("addr=0x0") != std::string::npos);
    // The offset points inside triggerNullWriteCrash().
    unsigned long long pcOff = std::strtoull(prev.crash.c_str() + sizeof("crash: SIGSEGV pc=eboot+0x") - 1, nullptr, 16);
    CHECK(pcOff > 0);
    CHECK(!xc::app::crash::takePrevious(marker).found);  // taken once

    // A signal raised inside a system library (std::raise) records that library's name and offset.
    pid = fork();
    if (pid == 0) {
        xc::app::crash::install(marker);
        xc::app::crash::setStage("home");
        std::raise(SIGABRT);
        _exit(0);
    }
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    prev = xc::app::crash::takePrevious(marker);
    CHECK(prev.found && prev.kind == "signal 6 (SIGABRT)" && prev.stage == "home");
    CHECK(prev.crash.rfind("crash: SIGABRT pc=", 0) == 0 && prev.crash.find("+0x") != std::string::npos);
}
#else
static void testCrashGuard() {
    std::string marker = platform::dataDir() + "/xcloud-tests-crash.marker";
    std::remove(marker.c_str());
    platform::writeFileAtomic(marker, "signal 11 (EXCEPTION_ACCESS_VIOLATION)\nstreaming TEST\ncrash: EXCEPTION_ACCESS_VIOLATION pc=psbox+0x1000 addr=0x0\n");
    auto prev = xc::app::crash::takePrevious(marker);
    CHECK(prev.found && prev.kind == "signal 11 (EXCEPTION_ACCESS_VIOLATION)" && prev.stage == "streaming TEST");
    CHECK(prev.crash.find("addr=0x0") != std::string::npos);
    CHECK(!xc::app::crash::takePrevious(marker).found);
}
#endif

static void testReport() {
    using namespace xc::app::report;
    // Private data out.
    // Made-up values (the tokens put together here, so the source holds none).
    const std::string jwt = std::string("ey") + "JmYWtlIjoidG9rZW4ifQ" + "QUJDREVGR0hJSktMTU5PUA.abc";
    const std::string xbl = std::string("XBL3.0 x=") + "12345;" + "ey" + "Jfake";
    std::string s = sanitize("signed in as SomePlayer from 203.0.113.7 token " + jwt + " auth " + xbl +
                                 " mail me@example.com id 0123456789abcdef0123456789abcdef ok",
                             {"SomePlayer"});
    CHECK(s.find("SomePlayer") == std::string::npos && s.find("<name>") != std::string::npos);
    CHECK(s.find("203.0.113") == std::string::npos && s.find("<ip>") != std::string::npos);
    CHECK(s.find("ey" "J") == std::string::npos && s.find("12345") == std::string::npos && s.find("<token>") != std::string::npos);
    CHECK(s.find("example.com") == std::string::npos && s.find("<email>") != std::string::npos);
    CHECK(s.find("0123456789abcdef0123") == std::string::npos && s.find("<id>") != std::string::npos);
    CHECK(sanitize("v0.9.5 at 1080p, 60 fps, 9 ms", {}) == "v0.9.5 at 1080p, 60 fps, 9 ms");  // nothing private
    CHECK(sanitize("crash: SIGSEGV pc=eboot+0x1a2b3c addr=0x0", {}) == "crash: SIGSEGV pc=eboot+0x1a2b3c addr=0x0");

    // The excerpt: the session wanted, its warnings / errors and last lines.
    std::string log = "10:00 [I] === PSBox Cloud Gaming starting ===\n10:00 [E] old error\n"
                      "11:00 [I] === PSBox Cloud Gaming starting ===\n";
    for (int i = 0; i < 40; ++i) log += "11:01 [I] line " + std::to_string(i) + "\n";
    log += "11:02 [W] a warning\n11:03 [I] tail\n";
    std::string now = logExcerpt(log, false, 5), before = logExcerpt(log, true, 5);
    CHECK(now.find("old error") == std::string::npos && now.find("a warning") != std::string::npos);
    CHECK(now.find("line 39") != std::string::npos && now.find("line 3\n") == std::string::npos && now.find("...") != std::string::npos);
    CHECK(before.find("old error") != std::string::npos && before.find("line") == std::string::npos);
    CHECK(logExcerpt("no sessions\n", true).empty());

    // The link fits the QR code; the log keeps its end, and a crash report
    // keeps its offset and app version in the issue body.
    Info info{"Error: certificates", "TLS setup failed", "0.9.6", "13.00", "", "crash: SIGSEGV pc=eboot+0x1a2b3c addr=0x0"};
    for (int i = 0; i < 200; ++i) info.log += "12:00 [I] a long line of the log number " + std::to_string(i) + "\n";
    std::string url = issueUrl(info, 1800);
    CHECK(url.size() <= 1800 && url.find("template=bug_report.yml") != std::string::npos);
    CHECK(url.find("number+199") != std::string::npos && url.find("number+0%0A") == std::string::npos);
    CHECK(url.find("release=0.9.6") != std::string::npos && url.find("title=Error:+certificates") != std::string::npos);
    CHECK(url.find("pc%3Deboot%2B0x1a2b3c") != std::string::npos && url.find("(v0.9.6)") != std::string::npos);
    CHECK(xc::platform::imageCacheBudget() >= (48u << 20));

    // QR code generation and 4-slot LRU caching
    ui::Canvas canvas(300, 300);
    auto qr1 = ui::drawQr(canvas, url, 280, 20, 200, qrcodegen_Ecc_LOW);
    CHECK(qr1.w > 0 && qr1.h > 0);
    auto qr1_cached = ui::drawQr(canvas, url, 280, 20, 200, qrcodegen_Ecc_LOW);
    CHECK(qr1_cached.w == qr1.w && qr1_cached.h == qr1.h);
    auto qr_empty = ui::drawQr(canvas, "", 280, 20, 200);
    CHECK(qr_empty.w == 0 && qr_empty.h == 0);
}

void runCoreTests() {
    testJson();
    testUrl();
    testBuiltinCa();
    testHttpKeepAlive();
    testResolveIPv4AndConnectTcp();
    testSocketLayer();
    testFsLayer();
    testSteamDeckDmi();
    testNullBackends();
    testNullBackendsM1();
    testToasts();
    testTrophyNonConsoleDisabled();
    testCrashGuard();
    testReport();
}
