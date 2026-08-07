#!/bin/bash

set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$DIR/common.sh"

pushd "$DIR/../libc"
    meson setup --prefix="$SYSROOT/usr" --cross-file="$DIR/meson-crossfile.txt" -Dheaders_only=true build
    meson install -C build
popd

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    download_and_extract GCC
popd

mkdir -p "$DIR/build_gcc"
pushd "$DIR/build_gcc"
    echo "configuring $GCC_NAME..."

    pushd "$DIR"/tarballs/"$GCC_NAME"/libstdc++-v3
        "$PREFIX"/bin/autoconf
    popd

    "$DIR"/tarballs/"$GCC_NAME"/configure \
        --prefix="$PREFIX" \
        --target="$TARGET" \
        --with-sysroot="$SYSROOT" \
        --disable-multilib \
        --disable-nls \
        --disable-werror \
        --enable-initfini-array \
        --enable-host-shared \
        --enable-languages=c,c++ \
        --enable-lto \
        --enable-shared \
        --enable-threads=posix \
        --with-pic \
        --with-system-zlib \
        --without-docdir

    echo "building $GCC_NAME..."

    make -j "$NPROC" all-gcc all-target-libgcc
    make install-strip-gcc install-target-libgcc

    pushd "$DIR/../libc"
        meson configure build -Dheaders_only=false
        meson compile --jobs "$NPROC" -C build
        meson install -C build
    popd

    make -j "$NPROC" all-target-libstdc++-v3
    make install-target-libstc++-v3
popd

rm -rf "$DIR/build_gcc"

