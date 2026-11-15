#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# Keeps the PS5's system calls inside the PS5 backends, and raw POSIX socket/fs
# calls inside portable abstraction layers, so shared code builds cleanly for
# Linux, Windows, and Android (docs/multiplatform.md).
#
# Fails, naming each line, when:
#   1. A file outside allowed PS5 backends has PS5 system calls, SDK headers, or switches.
#   2. A file outside allowed abstraction layers has raw POSIX socket/fs headers:
#      <sys/socket.h>, <arpa/inet.h>, <netdb.h>, <poll.h>, <sys/ioctl.h>,
#      <dirent.h>, <sys/stat.h>.
#
# Comments don't count. CI runs it on every push; locally:
#   bash tools/check-platform-boundaries.sh
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

# --- 1. Console Platform Boundary Rules --------------------------------------
allowed=(
    'src/platform/ps5/*'   # the PS5's platform layer
    'src/*_ps5.cpp'        # PS5 backends: display, controller, kbm, hw_decoder, shell...
    'ps5/*'                # link stubs and compat headers for the PS5 SDK
    'src/platform/ps4/*'   # the PS4's platform layer
    'src/*_ps4.cpp'        # PS4 backends: display, controller, hw_decoder, shell...
    'ps4/*'                # link stubs and metadata for PS4
)

is_allowed() {
    local f=$1 pattern
    for pattern in "${allowed[@]}"; do
        # shellcheck disable=SC2053  # the right side is a glob on purpose
        [[ $f == $pattern ]] && return 0
    done
    return 1
}

files=()
while IFS= read -r -d '' file; do
    file=${file#./}
    is_allowed "$file" || files+=("$file")
done < <(find src tests ps5 ps4 -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | sort -z)

scan='
use strict;
use warnings;
my @rules = (
    qr/\bsce[A-Z][A-Za-z0-9_]*/,
    qr/^\s*#\s*include\s*[<"](?:ps5platform|orbis)\//,
    qr/\b(?:XCLOUD_PS5|XC_PLATFORM_PS5|XC_PLATFORM_PS4|XC_TARGET_PS4|__PROSPERO__|__ORBIS__|__PS4__)\b/,
    qr/^\s*#\s*(?:if|ifdef|ifndef|elif|elifdef|elifndef)\b.*\b__FreeBSD__\b/,
    qr/^\s*#\s*(?:ifndef|elifndef)\s+XC_PLATFORM_DESKTOP\b/,
    qr/^\s*#\s*(?:if|elif)\b.*!\s*(?:defined\s*\(?\s*)?XC_PLATFORM_DESKTOP\b/,
);
my $found = 0;
for my $file (@ARGV) {
    open(my $fh, "<", $file) or die "$file: $!\n";
    my $src = do { local $/; <$fh> };
    close $fh;
    my @lines = split /\n/, $src, -1;
    (my $code = $src) =~ s{
        ( (?<!\w)(?:u8|[uUL])?R"([^()\\\s]{0,16})\(.*?\)\2" | "(?:\\.|[^"\\\n])*" | \x27(?:\\.|[^\x27\\\n])*\x27 )
      | ( //[^\n]* | /\*.*?\*/ )
    }{ defined $3 ? ($3 =~ s/[^\n]/ /gr) : $1 }gsex;
    my $n = 0;
    for my $line (split /\n/, $code, -1) {
        ++$n;
        next unless grep { $line =~ $_ } @rules;
        print "$file:$n:$lines[$n - 1]\n";
        $found = 1;
    }
}
exit $found;
'

boundary_failed=0

if (( ${#files[@]} )) && ! perl -e "$scan" -- "${files[@]}"; then
    echo "PS5 code outside the PS5 backends (above). Move it into a *_ps5.cpp backend behind a" >&2
    echo "portable header, or see docs/multiplatform.md; tools/check-platform-boundaries.sh lists" >&2
    echo "the files allowed to have it." >&2
    boundary_failed=1
fi

# --- 2. POSIX Socket and Filesystem Boundary Rules ---------------------------
# Raw POSIX socket and filesystem headers must stay inside the abstraction implementations
# (src/net/socket.h, src/net/socket.cpp, src/util/fs.h, src/util/fs.cpp) or platform backends.
allowed_net_fs=(
    'src/net/socket.h'            # portable socket header
    'src/net/socket.cpp'          # portable socket implementation
    'src/util/fs.h'               # portable filesystem header
    'src/util/fs.cpp'             # portable filesystem implementation
    'src/platform/*'              # platform layer backends
    'src/*_ps5.cpp'               # PS5 backends
    'ps5/*'                       # PS5 SDK stubs and headers
    'src/*_ps4.cpp'               # PS4 backends
    'src/app/ps4_main.cpp'        # PS4 entrypoint
    'ps4/*'                       # PS4 SDK stubs and headers
    'src/trophy/*'                # console-only trophy subsystems (PS5/PS4)
)

is_allowed_net_fs() {
    local f=$1 pattern
    for pattern in "${allowed_net_fs[@]}"; do
        # shellcheck disable=SC2053
        [[ $f == $pattern ]] && return 0
    done
    return 1
}

files_net_fs=()
while IFS= read -r -d '' file; do
    file=${file#./}
    is_allowed_net_fs "$file" || files_net_fs+=("$file")
done < <(find src tests ps5 ps4 -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | sort -z)

scan_net_fs='
use strict;
use warnings;
my @rules = (
    qr/^\s*#\s*include\s*[<"](?:sys\/socket|arpa\/inet|netdb|poll|sys\/ioctl|dirent|sys\/stat)\.h[>"]/,
);
my $found = 0;
for my $file (@ARGV) {
    open(my $fh, "<", $file) or die "$file: $!\n";
    my $src = do { local $/; <$fh> };
    close $fh;
    my @lines = split /\n/, $src, -1;
    (my $code = $src) =~ s{
        ( (?<!\w)(?:u8|[uUL])?R"([^()\\\s]{0,16})\(.*?\)\2" | "(?:\\.|[^"\\\n])*" | \x27(?:\\.|[^\x27\\\n])*\x27 )
      | ( //[^\n]* | /\*.*?\*/ )
    }{ defined $3 ? ($3 =~ s/[^\n]/ /gr) : $1 }gsex;
    my $n = 0;
    for my $line (split /\n/, $code, -1) {
        ++$n;
        next unless grep { $line =~ $_ } @rules;
        print "$file:$n:$lines[$n - 1]\n";
        $found = 1;
    }
}
exit $found;
'

if (( ${#files_net_fs[@]} )) && ! perl -e "$scan_net_fs" -- "${files_net_fs[@]}"; then
    echo "Raw POSIX socket or filesystem headers outside allowed layers (above). Move them behind" >&2
    echo "xc::net::Socket (src/net/socket.h) or xc::fs (src/util/fs.h); see docs/multiplatform.md." >&2
    boundary_failed=1
fi

if (( boundary_failed )); then
    exit 1
fi

echo "platform boundaries: clean"
