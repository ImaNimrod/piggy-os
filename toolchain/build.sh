#!/bin/bash

set -eu

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UNAME=$(uname)

NPROC=1
if [ ${UNAME} == "Darwin" ]; then
    NPROC=$(sysctl -n hw.ncpu)
elif [ ${UNAME} == "Linux" ]; then
    NPROC=$(nproc)
else
    echo "trying to build toolchain on possibly unsupported host platform..."
fi

source $DIR/toolchain.config

export CFLAGS="-g0 -O2 -mtune=native -pipe"
export CXXFLAGS="-g0 -O2 -mtune=native -pipe"
export PATH="$PATH:$PREFIX/bin"

function download_and_extract() {
    declare -n PKG=${1^^}_PKG
    declare -n NAME=${1^^}_NAME
    declare -n BASE_URL=${1^^}_BASE_URL
    declare -n MD5SUM=${1^^}_MD5SUM

    if [ ! -d ${NAME} ]; then
        local md5=""

        if [ -e ${PKG} ]; then
            md5="$(md5sum ${PKG} | cut -f1 -d ' ')"
        fi

        if [ "$md5" != ${MD5SUM} ] ; then
            rm -f ${PKG}
            echo "downloading ${PKG}..."
            curl -LO ${BASE_URL}/${PKG}

            md5="$(md5sum ${PKG} | cut -f1 -d ' ')"
            if [ "$md5" != ${MD5SUM} ] ; then
                echo "md5sum comparision failed for ${PKG}"
                exit 1
            fi
        else
            echo "skipped downloading ${NAME}"
        fi

        echo "extracting ${NAME}..."
        tar -xf ${PKG}
    else
        echo "using existing ${NAME} source"
    fi
}

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    download_and_extract BINUTILS
    download_and_extract GCC
    download_and_extract QEMU
popd

mkdir -p ${DIR}/build
pushd ${DIR}/build
    rm -rf build_*

mkdir -p build_binutils
    pushd build_binutils
        echo "configuring ${BINUTILS_NAME}..."

        "$DIR"/tarballs/"$BINUTILS_NAME"/configure \
            --prefix="$PREFIX" \
            --target="$TARGET" \
            --disable-nls \
            --disable-shared \
            --disable-werror \
            --enable-lto \
            --enable-static \
            --with-sysroot || exit 1

        echo "building ${BINUTILS_NAME}..."

        make -j "$NPROC" || exit 1
        make install || exit 1
    popd

    mkdir -p build_gcc
    pushd build_gcc
        echo "configuring ${GCC_NAME}..."

        EXTRA_ARGS=""
        if [ ${UNAME} == "Darwin" ]; then
            EXTRA_ARGS="--with-mpc=/opt/homebrew --with-gmp=/opt/homebrew --with-mpfr=/opt/homebrew"
        fi

        "$DIR"/tarballs/"$GCC_NAME"/configure \
            --prefix="$PREFIX" \
            --target="$TARGET" \
            --disable-nls \
            --disable-werror \
            --enable-languages=c \
            --enable-lto \
            --without-docdir \
            $EXTRA_ARGS || exit 1

        echo "building ${GCC_NAME}..."

        make all-gcc all-target-libgcc -j "$NPROC" || exit 1
        make install-gcc install-target-libgcc || exit 1
    popd

    mkdir -p build_qemu
    pushd build_qemu
        echo "configuring ${QEMU_NAME}..."

        EXTRA_ARGS=""
        if [ ${UNAME} == "Darwin" ]; then
            UI_LIB=cocoa
            EXTRA_ARGS="--disable-sdl"
        else
            UI_LIB=gtk
        fi

        ${DIR}/tarballs/${QEMU_NAME}/configure \
            --prefix=${PREFIX} \
            --target-list=x86_64-softmmu \
            --enable-$UI_LIB \
            --enable-slirp \
            $EXTRA_ARGS || exit 1

        echo "building ${QEMU_NAME}..."

        make -j $NPROC || exit 1
        make install || exit 1
    popd

    rm -rf build_qemu
popd
