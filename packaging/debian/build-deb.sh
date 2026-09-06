#!/bin/sh
# build-deb.sh - builds a .deb package for dotfile-manager.
#
# Usage: run from packaging/debian/ (or anywhere; paths are computed
# relative to this script). Produces
# ../dotfile-manager_<version>_<arch>.deb
#
# SPDX-License-Identifier: MIT
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VERSION="1.0.0"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
PKG_NAME="dotfile-manager"
STAGE_DIR="$SCRIPT_DIR/stage"
OUT_DEB="$SCRIPT_DIR/../${PKG_NAME}_${VERSION}_${ARCH}.deb"

echo "==> Building release binary"
cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build-pkg" -DCMAKE_BUILD_TYPE=Release -DDFM_BUILD_TESTS=OFF >/dev/null
cmake --build "$ROOT_DIR/build-pkg" -j"$(nproc)"

echo "==> Staging package tree"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/DEBIAN"
mkdir -p "$STAGE_DIR/usr/bin"
mkdir -p "$STAGE_DIR/usr/share/man/man1"
mkdir -p "$STAGE_DIR/usr/share/doc/${PKG_NAME}"

install -m 0755 "$ROOT_DIR/build-pkg/dotfile-manager" "$STAGE_DIR/usr/bin/dotfile-manager"

gzip -9 -n -c "$ROOT_DIR/man/dotfile-manager.1" > "$STAGE_DIR/usr/share/man/man1/dotfile-manager.1.gz"
chmod 0644 "$STAGE_DIR/usr/share/man/man1/dotfile-manager.1.gz"

install -m 0644 "$ROOT_DIR/README.md" "$STAGE_DIR/usr/share/doc/${PKG_NAME}/README.md"
install -m 0644 "$ROOT_DIR/LICENSE" "$STAGE_DIR/usr/share/doc/${PKG_NAME}/copyright"

INSTALLED_SIZE="$(du -sk "$STAGE_DIR" | cut -f1)"

sed -e "s/@VERSION@/${VERSION}/" \
    -e "s/@ARCH@/${ARCH}/" \
    -e "s/@INSTALLED_SIZE@/${INSTALLED_SIZE}/" \
    "$SCRIPT_DIR/control.in" > "$STAGE_DIR/DEBIAN/control"

install -m 0755 "$SCRIPT_DIR/postinst" "$STAGE_DIR/DEBIAN/postinst"
install -m 0755 "$SCRIPT_DIR/prerm" "$STAGE_DIR/DEBIAN/prerm"

find "$STAGE_DIR" -type d -exec chmod 0755 {} \;

echo "==> Building .deb"
dpkg-deb --build --root-owner-group "$STAGE_DIR" "$OUT_DEB"

echo "==> Verifying with lintian (if available)"
if command -v lintian >/dev/null 2>&1; then
    lintian "$OUT_DEB" || true
else
    echo "    lintian not installed, skipping lint"
fi

echo "==> Contents:"
dpkg-deb -c "$OUT_DEB"
echo
echo "Built: $OUT_DEB"
