#!/bin/sh
# build-rpm.sh - builds an RPM package for dotfile-manager.
#
# Usage: run from packaging/rpm/ (or anywhere; paths are computed relative
# to this script). Produces an RPM under ~/rpmbuild/RPMS/<arch>/ and a
# source RPM under ~/rpmbuild/SRPMS/.
#
# SPDX-License-Identifier: MIT
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VERSION="1.0.0"
PKG_NAME="dotfile-manager"

RPMBUILD_ROOT="${HOME}/rpmbuild"
mkdir -p "$RPMBUILD_ROOT/SPECS" "$RPMBUILD_ROOT/SOURCES" "$RPMBUILD_ROOT/BUILD" \
         "$RPMBUILD_ROOT/RPMS" "$RPMBUILD_ROOT/SRPMS" "$RPMBUILD_ROOT/BUILDROOT"

# A user-writable, user-local RPM database. On a real Fedora/RHEL/openSUSE
# build host the system rpmdb under /var/lib/rpm is already writable by
# root and this override is unnecessary; it exists so this script also
# works unprivileged and on non-RPM-native distributions used purely as a
# cross build host.
RPMDB_DIR="${HOME}/.rpmdb-build"
mkdir -p "$RPMDB_DIR"
if [ ! -e "$RPMDB_DIR/rpmdb.sqlite" ]; then
    rpm --dbpath "$RPMDB_DIR" --initdb
fi

echo "==> Creating clean source tarball"
TARBALL_DIR="$(mktemp -d)"
TARBALL_STAGE="$TARBALL_DIR/${PKG_NAME}-${VERSION}"
mkdir -p "$TARBALL_STAGE"

# Only ship what the build actually needs: sources, headers, top-level
# CMakeLists.txt, man page, README, LICENSE. No build/ artifacts, no VCS
# metadata, no test binaries.
for item in CMakeLists.txt LICENSE README.md src include man cmake; do
    if [ -e "$ROOT_DIR/$item" ]; then
        cp -a "$ROOT_DIR/$item" "$TARBALL_STAGE/"
    fi
done
# The RPM build always uses -DDFM_BUILD_TESTS=OFF, but the top-level
# CMakeLists.txt still references tests/ via add_subdirectory when the
# option is ON; ship a minimal tests/CMakeLists.txt-free layout by simply
# omitting tests/ entirely (safe since DFM_BUILD_TESTS=OFF in %build).

tar -C "$TARBALL_DIR" -czf "$RPMBUILD_ROOT/SOURCES/${PKG_NAME}-${VERSION}.tar.gz" "${PKG_NAME}-${VERSION}"
rm -rf "$TARBALL_DIR"

cp "$SCRIPT_DIR/dotfile-manager.spec" "$RPMBUILD_ROOT/SPECS/"

echo "==> Running rpmbuild"
RPMBUILD_EXTRA_ARGS=""
if command -v dpkg >/dev/null 2>&1 && ! command -v dnf >/dev/null 2>&1 && ! command -v yum >/dev/null 2>&1 && ! command -v zypper >/dev/null 2>&1; then
    # This is a dpkg/apt-based host used purely as a cross build machine:
    # rpm cannot resolve the spec's RPM-named BuildRequires (cmake,
    # gcc-c++, json-devel) against dpkg's package database even though
    # equivalent packages are installed under their Debian names, so skip
    # the dependency check. On a native Fedora/RHEL/openSUSE build host
    # (where dnf/yum/zypper is present) this branch is skipped and
    # rpmbuild performs its normal dependency verification.
    echo "    (non-RPM-native host detected: passing --nodeps)"
    RPMBUILD_EXTRA_ARGS="--nodeps"
fi
# shellcheck disable=SC2086
rpmbuild --define "_topdir $RPMBUILD_ROOT" --define "_dbpath $RPMDB_DIR" \
    $RPMBUILD_EXTRA_ARGS -ba "$RPMBUILD_ROOT/SPECS/dotfile-manager.spec"

echo
echo "Built RPMs:"
find "$RPMBUILD_ROOT/RPMS" "$RPMBUILD_ROOT/SRPMS" -name "*${PKG_NAME}*${VERSION}*"
