#!/usr/bin/env bash
# Jelly5-IPTV — the PS5 app built on a shared Linux host with no root (Ubuntu 22.04, as on the
# Bytesized box), in one go: what app/scripts/build.sh needs that such a host lacks goes under
# $HOME, then the usual toolchain setup and build, gently.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   bash scripts/build-on-shared-host.sh          # the diagnostic test build (JELLY5_AVDIAG=1), as a release zip
#   DIAG=0 bash scripts/build-on-shared-host.sh   # the same without the diagnostic log
#   JOBS=n bash scripts/build-on-shared-host.sh   # compile jobs at a time (default 4: a shared box)
#
# - LLVM 18 (clang, lld, llvm-config): $LLVM_CONFIG if set, else ~/llvm18 (the LLVM release that
#   RUFFLE5 unpacks there), else that release downloaded into ~/llvm18 (1 GB, 7 GB unpacked). Its
#   tools want libtinfo.so.5, which Ubuntu 22.04 has only as an extra package; when the host lacks
#   it, Ubuntu's own libtinfo5 package is unpacked (dpkg-deb, no root) into ~/llvm18/lib, where the
#   tools look first ($ORIGIN/../lib).
# - nasm (the AV1 decoder's assembly): built from source into ~/.local/bin when it is missing.
# - meson + ninja: pip --user when missing.
# - Every compile at most $JOBS at a time, at the lowest CPU and disk priority. The build scripts
#   ask for every core (make -j$(nproc), getconf _NPROCESSORS_ONLN, ninja's default), and on a
#   shared host that is what once took the box down; small wrappers in ~/.jelly5-build-bin answer
#   $JOBS instead, for this build only.
# Logs go to toolchain/logs/. Running it again is safe: what is done is not done again.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOBS="${JOBS:-4}"
DIAG="${DIAG:-1}"
LLVM_DIR="${LLVM_DIR:-$HOME/llvm18}"
LOCAL="$HOME/.local"
SHIM="$HOME/.jelly5-build-bin"
LOGS="$ROOT/toolchain/logs"
UBUNTU_MIRROR="${UBUNTU_MIRROR:-https://archive.ubuntu.com/ubuntu}"
LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/clang+llvm-18.1.8-x86_64-linux-gnu-ubuntu-18.04.tar.xz"
LLVM_SHA="54ec30358afcc9fb8aa74307db3046f5187f9fb89fb37064cdde906e062ebf36"
NASM_V="2.16.03"
NASM_URL="https://www.nasm.us/pub/nasm/releasebuilds/${NASM_V}/nasm-${NASM_V}.tar.xz"
NASM_SHA="1412a1c760bbd05db026b6c0d1657affd6631cd0a63cddb6f73cc6d4aa616148"

log() { printf '\n==> %s\n' "$*"; }
ok()  { printf '  ok %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || die "JOBS must be a number (got '$JOBS')"
[[ -x "$ROOT/app/scripts/build.sh" ]] || die "run this inside a Jelly5-IPTV clone (no app/scripts/build.sh under $ROOT)"
mkdir -p "$LOGS" "$LOCAL/bin" "$SHIM"
export PATH="$LOCAL/bin:$PATH"

NICE=(nice -n 19)
command -v ionice >/dev/null 2>&1 && NICE+=(ionice -c 2 -n 7)

# run LOG WHAT cmd...: the command at low priority, its output in LOG, one line a minute meanwhile
MON=""
trap '[[ -n "$MON" ]] && kill "$MON" 2>/dev/null; true' EXIT
run() {
    local log="$1" what="$2" start=$SECONDS rc=0; shift 2
    ( while sleep 60; do
          kill -0 $$ 2>/dev/null || exit 0
          printf '    %s: %d min, %s\n' "$what" $(( (SECONDS - start) / 60 )) \
              "$(grep -a -E '^(==>|  ok )' "$log" 2>/dev/null | tail -1 | cut -c1-90 || true)"
      done ) &
    MON=$!
    "${NICE[@]}" "$@" > "$log" 2>&1 || rc=$?
    kill "$MON" 2>/dev/null || true; wait "$MON" 2>/dev/null || true; MON=""
    if (( rc != 0 )); then
        echo "  --- the last 40 lines of $log:"
        tail -40 "$log"
        die "$what failed (exit $rc). The whole log: $log"
    fi
}

# ---- 1. host tools --------------------------------------------------------------------------
log "1/6 host tools"
for t in git make curl tar xz gzip unzip zip python3 pkg-config sha256sum; do
    command -v "$t" >/dev/null || die "'$t' is missing; on a shared host only the host can install it"
done
if ! command -v meson >/dev/null || ! command -v ninja >/dev/null; then
    echo "  meson + ninja for your user (pip --user; no root)"
    python3 -m pip install --user --upgrade meson ninja > "$LOGS/pip.log" 2>&1 ||
        { tail -20 "$LOGS/pip.log"; die "pip install --user meson ninja failed"; }
    hash -r
fi
command -v meson >/dev/null && command -v ninja >/dev/null ||
    die "meson or ninja is still not on PATH (pip puts them in ~/.local/bin)"
ok "meson $(meson --version), ninja $(ninja --version)"

# ---- 2. LLVM 18 -----------------------------------------------------------------------------
log "2/6 LLVM 18 (clang, lld, llvm-config)"
llvm_runs() {
    "$1/clang" --version >/dev/null 2>&1 && "$1/ld.lld" --version >/dev/null 2>&1 &&
        "$1/llvm-config" --version >/dev/null 2>&1
}
if [[ -n "${LLVM_CONFIG:-}" ]]; then
    cfg="$(command -v "$LLVM_CONFIG")" || die "LLVM_CONFIG=$LLVM_CONFIG is not a program"
    LLVM_BIN="$("$cfg" --bindir 2>/dev/null || dirname "$cfg")"
    echo "  LLVM_CONFIG=$LLVM_CONFIG"
elif [[ -x "$LLVM_DIR/bin/llvm-config" ]]; then
    LLVM_BIN="$LLVM_DIR/bin"
    echo "  $LLVM_DIR, already there"
else
    echo "  downloading LLVM 18.1.8 into $LLVM_DIR (1 GB, 7 GB unpacked)"
    tmp="$HOME/.llvm18-download.tar.xz"
    curl -fL --retry 3 -o "$tmp" "$LLVM_URL" || die "the LLVM download failed: $LLVM_URL"
    if ! echo "$LLVM_SHA  $tmp" | sha256sum -c --quiet -; then
        rm -f "$tmp"
        die "the LLVM download is not the expected file (deleted); run this again"
    fi
    mkdir -p "$LLVM_DIR"
    "${NICE[@]}" tar -xJf "$tmp" -C "$LLVM_DIR" --strip-components=1 || die "unpacking LLVM into $LLVM_DIR failed"
    rm -f "$tmp"
    LLVM_BIN="$LLVM_DIR/bin"
fi
if ! llvm_runs "$LLVM_BIN"; then
    if ldd "$LLVM_BIN/clang" 2>/dev/null | grep -q 'libtinfo\.so\.5 => not found'; then
        echo "  its tools want libtinfo.so.5, which this host lacks: Ubuntu's libtinfo5 package, unpacked beside them"
        command -v dpkg-deb >/dev/null || die "dpkg-deb not found (it is part of dpkg, on every Ubuntu)"
        codename="$( . /etc/os-release 2>/dev/null; echo "${VERSION_CODENAME:-jammy}" )"
        work="$(mktemp -d)"
        rec=""
        for comp in universe main; do
            curl -fsSL "$UBUNTU_MIRROR/dists/$codename/$comp/binary-amd64/Packages.gz" 2>/dev/null |
                gzip -dc > "$work/Packages" 2>/dev/null || continue
            rec="$(awk '/^Package: /{p = ($2 == "libtinfo5"); f = ""; s = ""}
                        p && /^Filename: /{f = $2}
                        p && /^SHA256: /{s = $2}
                        p && f != "" && s != "" {print f " " s; exit}' "$work/Packages")"
            [[ -n "$rec" ]] && break
        done
        [[ -n "$rec" ]] || die "no libtinfo5 package for Ubuntu $codename at $UBUNTU_MIRROR"
        fn="${rec% *}"; sha="${rec#* }"
        curl -fsSL -o "$work/libtinfo5.deb" "$UBUNTU_MIRROR/$fn" || die "the download failed: $UBUNTU_MIRROR/$fn"
        echo "$sha  $work/libtinfo5.deb" | sha256sum -c --quiet - || die "libtinfo5 does not match Ubuntu's checksum"
        dpkg-deb -x "$work/libtinfo5.deb" "$work/x" || die "dpkg-deb -x failed"
        found=0
        for d in "$work/x/lib/x86_64-linux-gnu" "$work/x/usr/lib/x86_64-linux-gnu"; do
            if compgen -G "$d/libtinfo.so.5*" >/dev/null; then cp -a "$d"/libtinfo.so.5* "$LLVM_BIN/../lib/"; found=1; fi
        done
        (( found )) || die "no libtinfo.so.5 in that package; send me: dpkg-deb -c $work/libtinfo5.deb"
        ok "libtinfo.so.5 in $(cd "$LLVM_BIN/../lib" && pwd)"
    fi
    llvm_runs "$LLVM_BIN" || { ldd "$LLVM_BIN/clang" 2>&1 | grep 'not found' || true; die "the LLVM at $LLVM_BIN does not run (above: what it lacks)"; }
fi
export LLVM_CONFIG="$LLVM_BIN/llvm-config"
ok "$("$LLVM_BIN/clang" --version | head -1)"

# ---- 3. nasm --------------------------------------------------------------------------------
log "3/6 nasm"
if ! command -v nasm >/dev/null; then
    echo "  building nasm $NASM_V into $LOCAL/bin (a minute)"
    work="$(mktemp -d)"
    curl -fsSL -o "$work/nasm.tar.xz" "$NASM_URL" || die "the nasm download failed: $NASM_URL"
    echo "$NASM_SHA  $work/nasm.tar.xz" | sha256sum -c --quiet - || die "the nasm download is not the expected file"
    tar -xJf "$work/nasm.tar.xz" -C "$work"
    hostcc=cc
    command -v cc >/dev/null || hostcc="$LLVM_BIN/clang"
    ( cd "$work/nasm-$NASM_V" && CC="$hostcc" "${NICE[@]}" ./configure --prefix="$LOCAL" &&
          "${NICE[@]}" make -j"$JOBS" ) > "$LOGS/nasm.log" 2>&1 ||
        { tail -30 "$LOGS/nasm.log"; die "building nasm failed (whole log: $LOGS/nasm.log)"; }
    install -m755 "$work/nasm-$NASM_V/nasm" "$work/nasm-$NASM_V/ndisasm" "$LOCAL/bin/"
    hash -r
    command -v nasm >/dev/null || die "nasm is built but not on PATH ($LOCAL/bin)"
fi
ok "$(nasm -v)"

# ---- 4. the job cap -------------------------------------------------------------------------
log "4/6 every compile at most $JOBS at a time, at the lowest priority"
real_getconf="$(command -v getconf)" || die "getconf not found"
real_ninja="$(command -v ninja)"
cat > "$SHIM/nproc" <<EOF
#!/bin/sh
echo $JOBS
EOF
cat > "$SHIM/getconf" <<EOF
#!/bin/sh
case "\$1" in _NPROCESSORS_ONLN|_NPROCESSORS_CONF) echo $JOBS ;; *) exec "$real_getconf" "\$@" ;; esac
EOF
cat > "$SHIM/ninja" <<EOF
#!/bin/sh
exec "$real_ninja" -j $JOBS "\$@"
EOF
chmod +x "$SHIM/nproc" "$SHIM/getconf" "$SHIM/ninja"
export PATH="$SHIM:$PATH"
[[ "$(nproc)" == "$JOBS" && "$(getconf _NPROCESSORS_ONLN)" == "$JOBS" ]] || die "the job cap did not take"
ok "nproc, getconf and ninja answer $JOBS; ${NICE[*]}"

# ---- 5. the PS5 toolchain -------------------------------------------------------------------
log "5/6 the PS5 toolchain: SDK, pacbrew sysroot, host zlib, AV1 (dav1d + FFmpeg 7.1)"
echo "  the first time 10-30 minutes on a shared box; then nothing to do"
cd "$ROOT"
run "$LOGS/setup.log" "toolchain" scripts/setup-toolchain.sh
ok "toolchain ready (log: $LOGS/setup.log)"

# ---- 6. the app -----------------------------------------------------------------------------
rev="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
log "6/6 Jelly5-IPTV $rev$([[ "$DIAG" == 1 ]] && echo ', diagnostic log on')"
cd "$ROOT/app"
eval "$(../scripts/setup-toolchain.sh --env)"
export PATH="$SHIM:$PATH"   # the cap in front again (the eval put LLVM and the SDK first)
if [[ "$DIAG" == 1 ]]; then export JELLY5_AVDIAG=1; else unset JELLY5_AVDIAG; fi
run "$LOGS/build.log" "build" scripts/build.sh --release
zip="$(ls -t "$ROOT"/app/build/app/Jelly5-*.zip 2>/dev/null | head -1)"
[[ -n "$zip" ]] || die "the build finished but left no zip in app/build/app/ (log: $LOGS/build.log)"
out="$HOME/Jelly5-IPTV-$rev$([[ "$DIAG" == 1 ]] && echo -diag).zip"
cp "$zip" "$out"
grep -a -E '^  ok |^integrity' "$LOGS/build.log" | tail -6
printf '\nDone: %s\n  sha256 %s\n' "$out" "$(sha256sum "$out" | cut -d' ' -f1)"
echo "Install it as the public zip is installed: close Jelly5 with the PS button first, then copy the"
echo "zip's PPSA99505 folder over the one on the console."
[[ "$DIAG" == 1 ]] && echo "The diagnostic log is then at /data/evoplayer/jelly5-diag.log on the console."
true
