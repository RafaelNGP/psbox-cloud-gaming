# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# CMake toolchain file for cross-compiling xCloud-PS5 for Windows (x86_64) using llvm-mingw (UCRT)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_CROSSCOMPILING TRUE)

if(NOT XC_MINGW_ROOT)
    if(DEFINED ENV{XC_MINGW_ROOT})
        set(XC_MINGW_ROOT "$ENV{XC_MINGW_ROOT}")
    else()
        set(XC_MINGW_ROOT "${XDG_CACHE_HOME}/xcloud-ps5/llvm-mingw-20261104")
        if(NOT EXISTS "${XC_MINGW_ROOT}")
            set(XC_MINGW_ROOT "$ENV{HOME}/.cache/xcloud-ps5/llvm-mingw-20261104")
        endif()
    endif()
endif()
set(XC_MINGW_ROOT "${XC_MINGW_ROOT}" CACHE PATH "llvm-mingw root")

if(NOT EXISTS "${XC_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang")
    message(FATAL_ERROR "XC_MINGW_ROOT=${XC_MINGW_ROOT} does not look like an llvm-mingw toolchain (missing bin/x86_64-w64-mingw32-clang). Run tools/windows/fetch-toolchain.sh first.")
endif()

set(CMAKE_C_COMPILER   "${XC_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${XC_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER  "${XC_MINGW_ROOT}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_AR           "${XC_MINGW_ROOT}/bin/llvm-ar")
set(CMAKE_RANLIB       "${XC_MINGW_ROOT}/bin/llvm-ranlib")

list(APPEND CMAKE_FIND_ROOT_PATH "${XC_MINGW_ROOT}/x86_64-w64-mingw32")
if(DEFINED CMAKE_PREFIX_PATH)
    list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH})
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Static runtime linking: libc++, libunwind, winpthreads into executable
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
