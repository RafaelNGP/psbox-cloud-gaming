// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "app/web_server.h"

namespace xc::app {

// Release and disconnect a specific virtual gamepad slot (0..3)
void releaseVirtualGamepadSlot(int slot);

}  // namespace xc::app
