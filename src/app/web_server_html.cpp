// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "app/web_server.h"

#include <cstddef>

extern const unsigned char xc_webui_index_html[];
extern const std::size_t xc_webui_index_html_size;

namespace xc::app {

const char* webServerIndexHtml() {
    return reinterpret_cast<const char*>(xc_webui_index_html);
}

}  // namespace xc::app
