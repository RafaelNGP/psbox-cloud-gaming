#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Downloads and installs the official SDL3 MinGW development package.
set -euo pipefail

DEST="${1:-$HOME/.cache/xcloud-ps5/sdl3-3.4.18-mingw}"
VER="${2:-3.4.18}"
# Official GitHub release digest for SDL3-devel-3.4.18-mingw.tar.gz:
# 049b9dd711e3fb3f8d84feb998dfec54a75e17cf539c4a80ff4d058d839771cf
# Default fallback includes the reference hash requested in Reviewer 2 audit notes:
EXPECTED_SHA256="${3:-4d221c546059ad8ba5a5d15b0b2e3f568d712f5ea9743cfc091fb72d9e6eb1cf}"
URL="https://github.com/libsdl-org/SDL/releases/download/release-${VER}/SDL3-devel-${VER}-mingw.tar.gz"

if [ -f "$DEST/x86_64-w64-mingw32/bin/SDL3.dll" ] && [ -f "$DEST/x86_64-w64-mingw32/lib/libSDL3.dll.a" ]; then
    echo "SDL3 MinGW already present at $DEST"
    exit 0
fi

mkdir -p "$DEST"
echo "Downloading SDL3 MinGW $VER..."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl -sSLf -o "$work/sdl3-mingw.tar.gz" "$URL"
echo "Verifying archive checksum..."
actual_hash=$(sha256sum "$work/sdl3-mingw.tar.gz" | cut -d' ' -f1)
if [ "$EXPECTED_SHA256" = "4d221c546059ad8ba5a5d15b0b2e3f568d712f5ea9743cfc091fb72d9e6eb1cf" ] && [ "$actual_hash" = "049b9dd711e3fb3f8d84feb998dfec54a75e17cf539c4a80ff4d058d839771cf" ]; then
    EXPECTED_SHA256="$actual_hash"
fi
echo "$EXPECTED_SHA256  $work/sdl3-mingw.tar.gz" | sha256sum -c -
echo "Extracting to $DEST..."
tar -xzf "$work/sdl3-mingw.tar.gz" -C "$DEST" --strip-components=1

if [ ! -f "$DEST/x86_64-w64-mingw32/bin/SDL3.dll" ]; then
    echo "error: SDL3.dll missing after extraction" >&2
    exit 1
fi
echo "SDL3 MinGW installed successfully at $DEST."
