#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Download JieLi's Linux toolchain (clang 4.0.1 for pi32v2) and link it as DEST/toolchain.
#   tools/get_toolchain.sh [DEST]      (default: ~/.jieli)
# The version is pinned and checked: JieLi's own link (pkgman.jieliapp.com/s/linux-toolchain) moves to newer
# releases, and a different compiler changes the build and its CPU budget.
set -e
DEST="${1:-$HOME/.jieli}"
VER=20250324.1
SHA=f686586bcfb45e0f0bb27fd2b39c7a7f313cb4f0e88a66a14da621ffa8225958
URL="https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-$VER.tar.xz"
mkdir -p "$DEST"
TMP="$DEST/jieli-linux-toolchains-$VER.tar.xz.part"
curl -fL --retry 3 -o "$TMP" "$URL"
if command -v sha256sum >/dev/null; then GOT=$(sha256sum "$TMP"); else GOT=$(shasum -a 256 "$TMP"); fi   # (macOS)
[ "${GOT%% *}" = "$SHA" ] || { rm -f "$TMP"; echo "toolchain: checksum mismatch for $URL"; exit 1; }
tar -xJf "$TMP" -C "$DEST"
rm -f "$TMP"
ln -sfn "jieli-linux-toolchains-$VER" "$DEST/toolchain"
echo "JIELI_TOOLCHAIN=$DEST/toolchain (jieli-linux-toolchains-$VER)"
