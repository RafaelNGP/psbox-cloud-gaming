#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Builds or packages the Windows portable zip of the desktop app.
#
#   tools/desktop/package-windows.sh <build-dir> [out-dir]
#       -> <out-dir>/PSBox-<version>-windows-x86_64.zip
#       -> <out-dir>/PSBox-<version>-windows-x86_64.zip.sha256
set -euo pipefail

build=${1:?usage: package-windows.sh <build-dir> [out-dir]}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$build"
build=$(cd "$build" && pwd)
out=${2:-$build/pkg}
mkdir -p "$out"
out=$(cd "$out" && pwd)
fail() { echo "package-windows: $*" >&2; exit 1; }
ok() { echo "  ok  $*"; }

project=$(sed -n 's/^project(xcloud_ps5 VERSION \([0-9.]*\) .*/\1/p' "$root/CMakeLists.txt")
version=${XC_VERSION:-v$project-dev}
name=PSBox-$version-windows-x86_64
zipfile=$out/$name.zip
shafile=$out/$name.zip.sha256

echo "== locate binaries ($build)"
exe=""
for candidate in "$build/psbox.exe" "$build/bin/psbox.exe"; do
    if [[ -f "$candidate" ]]; then
        exe="$candidate"
        break
    fi
done
[[ -n "$exe" ]] || fail "psbox.exe not found in $build; build the target first"
[[ -s "$exe" ]] || fail "psbox.exe is an empty file (0 bytes)"
ok "using binary: $exe -> psbox.exe"

sdl=""
for candidate in "$build/SDL3.dll" "$build/bin/SDL3.dll" \
                 "${XC_SDL3_MINGW:-$HOME/.cache/xcloud-ps5/sdl3-3.4.18-mingw}/x86_64-w64-mingw32/bin/SDL3.dll" \
                 "${XC_MINGW_ROOT:-$HOME/.cache/xcloud-ps5/llvm-mingw-20261104}/x86_64-w64-mingw32/bin/SDL3.dll"; do
    if [[ -f "$candidate" ]]; then
        sdl="$candidate"
        break
    fi
done
[[ -n "$sdl" ]] || fail "SDL3.dll not found; ensure SDL3 MinGW is installed"
[[ -s "$sdl" ]] || fail "SDL3.dll is an empty file (0 bytes)"
ok "using SDL3: $sdl -> SDL3.dll"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

echo "== install into stage directory"
cmake --install "$build" --prefix "$stage" >/dev/null
pkgdir=$stage/PSBox
[[ -d "$pkgdir" ]] || fail "cmake --install failed to create PSBox folder"

# Fallback copy if cmake --install did not bundle SDL3.dll
if [[ ! -f "$pkgdir/SDL3.dll" ]]; then
    cp "$sdl" "$pkgdir/SDL3.dll"
fi

echo "== create zip ($zipfile)"
rm -f "$zipfile" "$shafile"
python3 - << PY
import os
import zipfile

stage_dir = "$stage"
zip_path = "$zipfile"

with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
    for root_dir, dirs, files in os.walk(stage_dir):
        for file in files:
            full_path = os.path.join(root_dir, file)
            rel_path = os.path.relpath(full_path, stage_dir)
            zf.write(full_path, rel_path)
PY

[[ -s "$zipfile" ]] || fail "failed to create $zipfile"
ok "created $zipfile ($(du -h "$zipfile" | cut -f1))"

echo "== generate sha256 checksum ($shafile)"
(cd "$out" && sha256sum "$name.zip" > "$name.zip.sha256")
[[ -s "$shafile" ]] || fail "failed to generate $shafile"
ok "checksum: $(cat "$shafile")"

echo "== check package contents"
python3 - << PY
import sys
import zipfile
import re

required = [
    "PSBox/psbox.exe",
    "PSBox/SDL3.dll",
    "PSBox/README-windows.txt",
    "PSBox/assets/cacert.pem",
    "PSBox/assets/fonts/Inter-Regular.ttf",
    "PSBox/licenses/LICENSE",
    "PSBox/licenses/THIRD_PARTY_NOTICES.md",
]

forbidden = re.compile(r'(^|/)account\.json$|(^|/)autoplay\.txt$|\.log$|\.ppm$|\.aus$|\.h264$|\.key$|(^|/)settings\.json$')

with zipfile.ZipFile("$zipfile", 'r') as zf:
    names = set(zf.namelist())
    for r in required:
        if r not in names:
            print(f"Error: mandatory file missing from package: {r}", file=sys.stderr)
            sys.exit(1)
    for name in names:
        if not name.startswith("PSBox/"):
            print(f"Error: file outside PSBox/: {name}", file=sys.stderr)
            sys.exit(1)
        if forbidden.search(name) and not name.endswith("cacert.pem"):
            print(f"Error: forbidden file in package: {name}", file=sys.stderr)
            sys.exit(1)
    
    exe_info = zf.getinfo("PSBox/psbox.exe")
    if exe_info.file_size == 0:
        print("Error: PSBox/psbox.exe is 0 bytes (dummy file forbidden)", file=sys.stderr)
        sys.exit(1)
        
    sdl_info = zf.getinfo("PSBox/SDL3.dll")
    if sdl_info.file_size == 0:
        print("Error: PSBox/SDL3.dll is 0 bytes", file=sys.stderr)
        sys.exit(1)

print("  ok  clean package contents: all mandatory files verified, binary non-empty")
PY

(cd "$out" && sha256sum -c "$name.zip.sha256")
ok "Windows package check passed: $zipfile"

# Stage to NAS Dev directory if mounted and writable
nas_dir="/home/notebook-storm/wolfpad-NAS/Dev"
if [[ -d "$nas_dir" && -w "$nas_dir" ]]; then
    echo "== stage package to NAS ($nas_dir)"
    cp "$zipfile" "$nas_dir/"
    cp "$shafile" "$nas_dir/"
    ok "staged $name.zip and .sha256 to NAS"
fi
