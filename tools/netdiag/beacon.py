#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
"""Beacons for the stream-freeze diagnostics (src/stream/net_diag.h).

Sends small UDP packets to the console's port 9123, 25 a second in each of
four Wi-Fi priorities (BE, BK, VI, VO), while the console has netdiag.txt in
its data folder. During a freeze of the stream the app logs whether these
kept arriving: if they did, the console's Wi-Fi was receiving fine.

    python3 tools/netdiag/beacon.py 192.168.15.17 [seconds]
"""
import socket
import struct
import sys
import time

TOS = [0x00, 0x20, 0x88, 0xB8]  # BE, BK (CS1), VI (AF41), VO (EF)
RATE = 25                       # per priority, per second


def main():
    host = sys.argv[1]
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 3600
    socks = []
    for tos in TOS:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.IPPROTO_IP, socket.IP_TOS, tos)
        socks.append(s)
    step = 1.0 / (RATE * len(TOS))
    seq = [0] * len(TOS)
    start = time.monotonic()
    nxt = start
    n = 0
    while time.monotonic() - start < seconds:
        c = n % len(TOS)
        seq[c] += 1
        payload = b"PSBXBCN1" + struct.pack(">BIQ", c, seq[c], int(time.time() * 1e6))
        try:
            socks[c].sendto(payload, (host, 9123))
        except OSError:
            pass
        n += 1
        nxt += step
        delay = nxt - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        else:
            nxt = time.monotonic()


if __name__ == "__main__":
    main()
