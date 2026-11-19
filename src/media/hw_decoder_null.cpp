// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// No hardware decoder (anywhere but the PS5): init() fails, FFmpeg decodes.
#include <cstddef>
#include <cstdint>
#include "media/hw_decoder.h"

namespace xc::media {

HwDecoder::~HwDecoder() = default;
bool HwDecoder::init(int, int) { return false; }
bool HwDecoder::supportsQhd() { return true; }
bool HwDecoder::ensureConfig(int, int, uint32_t, uint32_t) { return false; }
bool HwDecoder::decode(const uint8_t*, size_t, uint64_t, Picture&) { return false; }

}  // namespace xc::media
