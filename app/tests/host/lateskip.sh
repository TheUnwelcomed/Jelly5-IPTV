#!/usr/bin/env bash
# Jelly5 — Jellyfin for PS5
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Host test of the late-picture skip (evo_late_skip.h) with ASan and UBSan: a
# simulated decoder, sound clock and presentation clock, no console or server
# needed.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/jelly5-lateskip.XXXXXX")
trap 'rm -rf -- "$build"' EXIT
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$root/app/engine/media/include" "$root/app/tests/host/lateskip_test.c" \
    -lm -o "$build/lateskip-test"
"$build/lateskip-test"
