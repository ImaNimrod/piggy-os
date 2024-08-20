#!/usr/bin/env bash

set -e

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

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    md5=""
    if [ -e ${QEMU_PKG} ]; then
        md5="$(md5sum ${QEMU_PKG} | cut -f1 -d ' ')"
    fi
    if [ "$md5" != ${QEMU_MD5SUM} ] ; then
        rm -f ${QEMU_PKG}
        echo "downloading ${QEMU_NAME}..."
        curl -LO ${QEMU_BASE_URL}/${QEMU_PKG}
    else
        echo "skipped downloading ${QEMU_NAME}"
    fi

    md5=""
    if [ -e ${BINUTILS_PKG} ]; then
        md5="$(md5sum ${BINUTILS_PKG} | cut -f1 -d ' ')"
    fi
    if [ "$md5" != ${BINUTILS_MD5SUM} ] ; then
        rm -f ${BINUTILS_PKG}
        echo "downloading ${BINUTILS_NAME}..."
        curl -LO ${BINUTILS_BASE_URL}/${BINUTILS_PKG}
    else
        echo "skipped downloading ${BINUTILS_NAME}"
    fi

    md5=""
    if [ -e "$GCC_PKG" ]; then
        md5="$(md5sum ${GCC_PKG} | cut -f1 -d ' ')"
    fi
    if [ "$md5" != ${GCC_MD5SUM} ] ; then
        rm -f ${GCC_PKG}
        echo "downloading ${GCC_NAME}..."
        curl -LO ${GCC_BASE_URL}/${GCC_NAME}/${GCC_PKG}
    else
        echo "skipped downloading ${GCC_NAME}"
    fi

    if [ ! -d ${QEMU_NAME} ]; then
        echo "extracting ${QEMU_NAME}..."
        tar -xf ${QEMU_PKG}
    else
        echo "using existing ${QEMU_NAME} source"
    fi

    if [ ! -d ${BINUTILS_NAME} ]; then
        echo "extracting ${BINUTILS_NAME}..."
        tar -xf ${BINUTILS_PKG}

        echo "patching ${BINUTILS_NAME}..."
        pushd ${BINUTILS_NAME}
            patch -p1 < ${DIR}/patches/${BINUTILS_NAME}.patch
        popd
    else
        echo "using existing ${BINUTILS_NAME} source"
    fi

    if [ ! -d ${GCC_NAME} ]; then
        echo "extracting ${GCC_NAME}..."
        tar -xf ${GCC_PKG}

        echo "patching ${GCC_NAME}..."
        pushd ${GCC_NAME}
            patch -p1 < ${DIR}/patches/${GCC_NAME}.patch
        popd
    else
        echo "using existing ${GCC_NAME} source"
    fi
popd

mkdir -p ${DIR}/build
pushd ${DIR}/build
    rm -rf build_*

    mkdir -p build_qemu
    pushd build_qemu
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

        make -j $NPROC || exit 1
        make install || exit 1
    popd

    mkdir -p build_binutils
    pushd build_binutils
        ${DIR}/tarballs/${BINUTILS_NAME}/configure \
            --prefix=${PREFIX} \
            --target=${TARGET} \
            --disable-nls \
            --disable-werror \
            --with-sysroot="$SYSROOT" || exit 1

        make -j $NPROC || exit 1
        make install || exit 1
    popd

    mkdir -p build_gcc
    pushd build_gcc
        EXTRA_ARGS=""
        if [ ${UNAME} == "Darwin" ]; then
            EXTRA_ARGS="--with-mpc=/opt/homebrew --with-gmp=/opt/homebrew --with-mpfr=/opt/homebrew"
        fi

        ${DIR}/tarballs/${GCC_NAME}/configure \
            --prefix=${PREFIX} \
            --target=${TARGET} \
            --disable-nls \
            --disable-werror \
            --enable-languages=c \
            --without-docdir \
            --with-sysroot="$SYSROOT" \
            $EXTRA_ARGS || exit 1

        make all-gcc all-target-libgcc -j $NPROC || exit 1
        make install-gcc install-target-libgcc || exit 1
    popd
popd
