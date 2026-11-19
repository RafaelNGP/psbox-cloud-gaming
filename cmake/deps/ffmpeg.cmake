# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# libavcodec/libswresample/libavutil (H.264 + Opus decoders) from deps/ffmpeg, built by
# tools/build-ffmpeg.sh into <build>/ffmpeg; run here when missing.

set(XC_FFMPEG_DIR "${CMAKE_BINARY_DIR}/ffmpeg")
if(NOT EXISTS "${XC_FFMPEG_DIR}/lib/libswresample.a")
    if(XCLOUD_PS5)
        set(_target ps5)
    elseif(XC_TARGET_PS4)
        set(_target ps4)
    elseif(WIN32)
        set(_target windows)
    elseif(ANDROID)
        set(_target android-${ANDROID_ABI})
    else()
        set(_target host)
    endif()
    message(STATUS "Building FFmpeg (${_target}), this takes a minute...")
    execute_process(
        COMMAND "${CMAKE_SOURCE_DIR}/tools/build-ffmpeg.sh" ${_target} "${CMAKE_BINARY_DIR}"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "tools/build-ffmpeg.sh failed")
    endif()
endif()

add_library(xc_ffmpeg INTERFACE)
target_include_directories(xc_ffmpeg INTERFACE "${XC_FFMPEG_DIR}/include")
target_link_libraries(xc_ffmpeg INTERFACE "${XC_FFMPEG_DIR}/lib/libavcodec.a"
    "${XC_FFMPEG_DIR}/lib/libswresample.a" "${XC_FFMPEG_DIR}/lib/libavutil.a")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID AND EXISTS "${XC_FFMPEG_DIR}/lib/pkgconfig/libavutil.pc")
    file(READ "${XC_FFMPEG_DIR}/lib/pkgconfig/libavutil.pc" _xc_avutil_pc)
    if(_xc_avutil_pc MATCHES "-lva([ \t\r\n]|$)")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(XC_LIBVA REQUIRED libva libva-drm)
        # Runtime dlopen() shim for libva.so.2 / libva-drm.so.2: linked after
        # libavcodec.a / libavutil.a so the binary (and AppImage) has zero
        # DT_NEEDED on libva* and always uses the host's native libva + drivers.
        add_library(xc_vaapi_loader STATIC "${CMAKE_SOURCE_DIR}/src/media/vaapi_dlopen.c")
        set_target_properties(xc_vaapi_loader PROPERTIES C_VISIBILITY_PRESET hidden)
        target_include_directories(xc_vaapi_loader PRIVATE ${XC_LIBVA_INCLUDE_DIRS})
        target_link_libraries(xc_vaapi_loader PRIVATE ${CMAKE_DL_LIBS})
        target_link_libraries(xc_ffmpeg INTERFACE xc_vaapi_loader)
        message(STATUS "FFmpeg VA-API enabled (runtime dlopen shim for libva.so.2 / libva-drm.so.2)")
    endif()
endif()
if(NOT XCLOUD_PS5 AND NOT XC_TARGET_PS4 AND NOT WIN32)
    target_link_libraries(xc_ffmpeg INTERFACE m)
endif()
if(WIN32)
    target_link_libraries(xc_ffmpeg INTERFACE bcrypt d3d11 dxgi)
endif()
