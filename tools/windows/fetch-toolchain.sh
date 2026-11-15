#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Download the llvm-mingw UCRT toolchain release into $1 (default: ~/.cache/xcloud-ps5/llvm-mingw-20261104)
set -euo pipefail

DEST="${1:-${XDG_CACHE_HOME:-$HOME/.cache}/xcloud-ps5/llvm-mingw-20261104}"
VERSION="20261104"
URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${VERSION}/llvm-mingw-${VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz"
EXPECTED_SHA256="919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926"

VK_VER="1.4.309"
VK_CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/xcloud-ps5/vulkan-headers-$VK_VER"

install_vulkan() {
    if [ ! -f "$DEST/x86_64-w64-mingw32/include/vulkan/vulkan.h" ]; then
        if [ ! -f "$VK_CACHE/include/vulkan/vulkan.h" ]; then
            mkdir -p "$VK_CACHE"
            echo "Fetching Vulkan-Headers $VK_VER..."
            curl -sSLf "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v$VK_VER.tar.gz" |
                tar xz -C "$VK_CACHE" --strip-components=1
        fi
        echo "Copying Vulkan headers into toolchain sysroot..."
        cp -r "$VK_CACHE/include/"* "$DEST/x86_64-w64-mingw32/include/"
    fi
}

if [ -x "$DEST/bin/x86_64-w64-mingw32-clang" ] && [ -x "$DEST/bin/x86_64-w64-mingw32-clang++" ]; then
    echo "llvm-mingw toolchain already present at $DEST"
    install_vulkan
    exit 0
fi

mkdir -p "$DEST"
TMP_ARCHIVE="$DEST/llvm-mingw.tar.xz"

echo "Downloading llvm-mingw from $URL..."
curl -sSL --fail -o "$TMP_ARCHIVE" "$URL"

echo "Verifying archive checksum..."
echo "$EXPECTED_SHA256  $TMP_ARCHIVE" | sha256sum -c -

echo "Extracting toolchain to $DEST..."
tar -xf "$TMP_ARCHIVE" -C "$DEST" --strip-components=1
rm -f "$TMP_ARCHIVE"

if [ ! -x "$DEST/bin/x86_64-w64-mingw32-clang" ]; then
    echo "error: $DEST does not contain x86_64-w64-mingw32-clang after extraction" >&2
    exit 1
fi
chmod +x "$DEST"/bin/* 2>/dev/null || true
install_vulkan

echo "llvm-mingw toolchain installed successfully at $DEST."
