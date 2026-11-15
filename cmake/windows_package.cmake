# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Windows portable package install rules for psbox.exe.

set(XC_PACKAGE_VERSION "v${PROJECT_VERSION}-dev" CACHE STRING
    "Version in the desktop package's name")

set(_pkg PSBox)
set(_src "${CMAKE_CURRENT_SOURCE_DIR}")

# The executable target
install(TARGETS psbox RUNTIME DESTINATION ${_pkg})

# Bundle SDL3.dll beside psbox.exe
if(TARGET SDL3::SDL3-shared)
    install(FILES "$<TARGET_FILE:SDL3::SDL3-shared>" DESTINATION ${_pkg})
endif()

# Fonts and CA bundle
install(DIRECTORY "${_src}/assets/" DESTINATION ${_pkg}/assets)

# Documentation and icon
install(FILES "${_src}/windows/README-windows.txt" DESTINATION ${_pkg})
if(EXISTS "${_src}/psbox.png")
    install(FILES "${_src}/psbox.png" DESTINATION ${_pkg})
elseif(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/psbox.png")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/psbox.png" DESTINATION ${_pkg})
endif()

# Licenses
install(FILES "${_src}/LICENSE" "${_src}/THIRD_PARTY_NOTICES.md" DESTINATION ${_pkg}/licenses)
set(_lic
    "assets/fonts/OFL.txt=Inter-OFL.txt"
    "deps/mbedtls/LICENSE=mbedtls-LICENSE.txt"
    "deps/libdatachannel/LICENSE=libdatachannel-MPL-2.0.txt"
    "deps/libdatachannel/deps/usrsctp/LICENSE.md=usrsctp-LICENSE.md"
    "deps/libdatachannel/deps/libsrtp/LICENSE=libsrtp-LICENSE.txt"
    "deps/libdatachannel/deps/plog/LICENSE=plog-LICENSE.txt"
    "deps/ffmpeg/COPYING.LGPLv2.1=FFmpeg-LGPL-2.1.txt"
    "extern/stb/LICENSE=stb-LICENSE.txt"
    "extern/fsr1/LICENSE.txt=FSR1-MIT.txt"
    "extern/anime4k/LICENSE=Anime4K-MIT.txt"
    "extern/nanosvg/LICENSE.txt=nanosvg-LICENSE.txt")
foreach(_pair IN LISTS _lic)
    string(REPLACE "=" ";" _pair "${_pair}")
    list(GET _pair 0 _from)
    list(GET _pair 1 _to)
    if(EXISTS "${_src}/${_from}")
        install(FILES "${_src}/${_from}" DESTINATION ${_pkg}/licenses RENAME "${_to}")
    endif()
endforeach()

# SDL3 License
find_file(XC_SDL3_LICENSE NAMES LICENSE.txt
    HINTS "${SDL3_DIR}/../../../share/licenses/SDL3"
          "${SDL3_DIR}/../../share/licenses/SDL3"
          "$ENV{HOME}/.cache/xcloud-ps5/sdl3-3.4.18-mingw/x86_64-w64-mingw32/share/licenses/SDL3"
    NO_DEFAULT_PATH)
if(XC_SDL3_LICENSE)
    install(FILES "${XC_SDL3_LICENSE}" DESTINATION ${_pkg}/licenses RENAME SDL3-LICENSE.txt)
endif()

unset(_pkg)
unset(_src)
