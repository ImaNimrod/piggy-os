#!/bin/bash

set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$DIR/common.sh"

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    download_and_extract BINUTILS
popd

mkdir -p "$DIR/build_binutils"
pushd "$DIR/build_binutils"
    echo "configuring $BINUTILS_NAME..."

    "$DIR"/tarballs/"$BINUTILS_NAME"/configure \
        --prefix="$PREFIX" \
        --target="$TARGET" \
        --with-sysroot="$SYSROOT" \
        --disable-multilib \
        --disable-nls \
        --disable-werror \
        --enable-default-execstack=no \
        --enable-initfini-array \
        --enable-lto \
        --enable-shared \
        --with-pic \
        --with-system-zlib \
        --without-docdir

    echo "building $BINUTILS_NAME..."

    make -j "$NPROC"
    make install
popd

rm -rf "$DIR/build_binutils"
