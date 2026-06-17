#!/bin/bash

set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$DIR/common.sh"

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    download_and_extract LIBTOOL
    download_and_extract PKGCONF
popd

mkdir -p "$DIR/build_libtool"
pushd "$DIR/build_libtool"
    echo "configuring ${LIBTOOL_NAME}..."

    "$DIR"/tarballs/"$LIBTOOL_NAME"/configure \
        --prefix="$PREFIX" \
        --disable-static \
        --enable-shared \
        --with-gnu-ld

    echo "building ${LIBTOOL_NAME}..."

    make -j "$NPROC" || exit 1
    make install || exit 1
popd

rm -rf "$DIR/build_libtool"

mkdir -p "$DIR/build_pkgconf"
pushd "$DIR/build_pkgconf"
    echo "configuring ${PKGCONF_NAME}..."

    "$DIR"/tarballs/"$PKGCONF_NAME"/configure \
        --prefix="$PREFIX"

    echo "building ${PKGCONF_NAME}..."

    make -j "$NPROC" || exit 1
    make install || exit 1
popd

rm -rf "$DIR/build_pkgconf"

mkdir -p ${PREFIX}/share/pkgconfig/personality.d

cat > ${PREFIX}/share/pkgconfig/personality.d/x86_64-piggy.personality << EOF
Triplet: x86_64-piggy
SysrootDir: ${SYSROOT}
DefaultSearchPaths: ${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig
SystemIncludePaths: ${SYSROOT}/usr/include
SystemLibraryPaths: ${SYSROOT}/usr/lib
EOF

pushd "$DIR/local/bin"
    ln -sf pkgconf x86_64-piggy-pkg-config
    ln -sf pkgconf pkg-config
popd
