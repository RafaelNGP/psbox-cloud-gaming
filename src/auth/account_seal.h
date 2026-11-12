// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// The saved sign-in (a refresh token) is kept sealed: AES-256-GCM under a key
// made from something only this device has (the console's id, the machine's
// id; where there is none, a random key file beside the data). A copy of the
// file that leaves the device (a backup, a shared folder, a leak) cannot be
// used elsewhere. It is not a defence against someone with full access to the
// device itself: the app must be able to open the file unattended.
#pragma once

#include <string>

namespace xc::auth {

// `plain` sealed as a small JSON document ({"version":2,"nonce","data"}); empty on failure.
std::string sealAccount(const std::string& plain);
// The reverse; false for anything that is not a sealed document made on this device.
bool unsealAccount(const std::string& sealed, std::string& plain);
// True when `text` is a sealed document (whoever made it).
bool isSealedAccount(const std::string& text);

}  // namespace xc::auth
