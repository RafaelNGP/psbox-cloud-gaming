#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
# An autotest's xcloud.log must not say that the app started without something it needs, even
# when the screenshot looks right (the fonts standing in from inside the binary still draw text):
#
#   tools/check-startup-log.sh <xcloud.log>     exit 1 and the lines when it does
set -euo pipefail
log=${1:?usage: check-startup-log.sh <xcloud.log>}
[ -f "$log" ] || { echo "STARTUP CHECK: no log at $log" >&2; exit 1; }
# What each means: the fonts' files not read (the app's folder out of reach or an incomplete
# install), no font at all, the assets folder not found, the data folder not writable.
patterns='fonts: .* not usable|fonts missing|a font is missing|assets: not found|TLS setup: .*failed|data folder .*not writable'
if bad=$(grep -a -n -E "$patterns" "$log"); then
    echo "STARTUP CHECK FAILED ($log):" >&2
    echo "$bad" | cut -c1-200 >&2
    exit 1
fi
echo "startup check: ok"
