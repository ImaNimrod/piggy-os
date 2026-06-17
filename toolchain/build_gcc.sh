#!/bin/bash

set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$DIR/common.sh"

pushd "$DIR/../libc"
    meson setup --prefix="${SYSROOT}/usr" --cross-file="$DIR"/meson-crossfile.txt -Dheaders_only=true build
    meson install -C build
popd

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    download_and_extract BINUTILS
    download_and_extract GCC
popd

mkdir -p "$DIR/build_binutils"
pushd "$DIR/build_binutils"
    echo "configuring ${BINUTILS_NAME}..."

    "$DIR"/tarballs/"$BINUTILS_NAME"/configure \
        --prefix="$PREFIX" \
        --target="$TARGET" \
        --disable-multilib \
        --disable-nls \
        --disable-werror \
        --enable-default-execstack=no \
        --enable-initfini-array \
        --enable-lto \
        --enable-shared \
        --with-sysroot="$SYSROOT" \
        --with-system-zlib \
        --without-docdir || exit 1

    echo "building ${BINUTILS_NAME}..."

    make -j "$NPROC" || exit 1
    make install || exit 1
popd

rm -rf "$DIR/build_binutils"

mkdir -p "$DIR/build_gcc"
pushd "$DIR/build_gcc"
    echo "configuring ${GCC_NAME}..."

    pushd "$DIR"/tarballs/"$GCC_NAME"/libstdc++-v3
        "$PREFIX"/bin/autoconf
    popd

    "$DIR"/tarballs/"$GCC_NAME"/configure \
        --prefix="$PREFIX" \
        --target="$TARGET" \
        --disable-multilib \
        --disable-nls \
        --disable-werror \
        --enable-initfini-array \
        --enable-host-shared \
        --enable-languages=c,c++,lto \
        --enable-lto \
        --enable-shared \
        --enable-threads=posix \
        --with-pic \
        --with-sysroot="$SYSROOT" \
        --with-system-zlib \
        --without-docdir || exit 1

    echo "building ${GCC_NAME}..."

    make -j "$NPROC" all-gcc all-target-libgcc all-target-libstdc++-v3 || exit 1
    make install-gcc install-target-libgcc install-target-libstdc++-v3 || exit 1
popd

rm -rf "$DIR/build_gcc"
