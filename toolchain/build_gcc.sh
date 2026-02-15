#!/bin/sh

set -eu

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$DIR/common.sh"

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    download_and_extract AUTOCONF
    download_and_extract BINUTILS
    download_and_extract GCC
popd

mkdir -p "$DIR/build"
pushd "$DIR/build"
    rm -rf build_*

    mkdir -p build_autoconf
    pushd build_autoconf
        echo "configuring ${AUTOCONF_NAME}..."

        "$DIR"/tarballs/"$AUTOCONF_NAME"/configure \
            --prefix="$PREFIX"

        echo "building ${AUTOCONF_NAME}..."

        make -j "$NPROC" || exit 1
        make install || exit 1
    popd

    rm -rf build_autoconf

    mkdir -p build_binutils
    pushd build_binutils
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

    rm -rf build_binutils

    mkdir -p build_gcc
    pushd build_gcc
        echo "configuring ${GCC_NAME}..."

        EXTRA_ARGS=""
        if [ ${UNAME} == "Darwin" ]; then
            EXTRA_ARGS="--with-mpc=/opt/homebrew --with-gmp=/opt/homebrew --with-mpfr=/opt/homebrew"
        fi

        pushd "$DIR"/tarballs/"$GCC_NAME"/libstdc++-v3
            "$DIR"/build/bin/autoconf
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
            --without-docdir \
            $EXTRA_ARGS || exit 1

        echo "building ${GCC_NAME}..."

        make all-gcc all-target-libgcc -j "$NPROC" || exit 1
        make install-gcc install-target-libgcc || exit 1

        make all-target-libstdc++-v3 -j "$NPROC" || exit 1
        make install-target-libstdc++-v3 || exit 1
    popd

    rm -rf build_gcc
popd
