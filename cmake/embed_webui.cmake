# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Assembles the Smartglass WebUI (HTML + CSS + JS + app version) and embeds it
# as a NUL-terminated C++ byte array (NAME[] and NAME_size), matching
# cmake/embed_file.cmake so it works identically across all platforms.
# Usage:
#   cmake -DHTML=... -DCSS=... -DJS=... -DVERSION=... -DOUT=... -DNAME=... -P cmake/embed_webui.cmake

file(READ "${HTML}" html)
file(READ "${CSS}" css)
file(READ "${JS}" js)

string(REPLACE "{{XC_WEBUI_STYLE}}" "${css}" html "${html}")
string(REPLACE "{{XC_WEBUI_SCRIPT}}" "${js}" html "${html}")
string(REPLACE "{{XC_APP_VERSION}}" "${VERSION}" html "${html}")

set(assembled "${OUT}.assembled")
file(WRITE "${assembled}" "${html}")
file(READ "${assembled}" hex HEX)
file(REMOVE "${assembled}")

string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")

file(WRITE "${OUT}" "// Generated from ${HTML}, ${CSS}, ${JS} by cmake/embed_webui.cmake: do not edit.\n#include <cstddef>\n\nextern const unsigned char ${NAME}[] = {\n${bytes}0x00};\nextern const std::size_t ${NAME}_size = sizeof(${NAME});\n")
