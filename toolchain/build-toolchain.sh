#!/bin/sh

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source $DIR/toolchain.config

export CFLAGS="-g0 -O2 -mtune=native -pipe"
export CXXFLAGS="-g0 -O2 -mtune=native -pipe"
export PATH="$PATH:$PREFIX/bin"

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
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
        pushd ${BINUTILS_NAME}
            patch -p1 < ${DIR}/patches/${GCC_NAME}.patch
        popd
    else
        echo "using existing ${GCC_NAME} source"
    fi
popd

mkdir -p ${DIR}/build
pushd ${DIR}/build
    rm -rf build_binutils
    rm -rf build_gcc

    mkdir -p build_binutils
    pushd build_binutils
        ${DIR}/tarballs/${BINUTILS_NAME}/configure \
            --prefix=${PREFIX} \
            --target=${TARGET} \
            --disable-nls \
            --disable-werror \
            --with-sysroot="$SYSROOT"

        make -j $(nproc)
        make install
    popd

    mkdir -p build_gcc
    pushd build_gcc
        ${DIR}/tarballs/${GCC_NAME}/configure \
            --prefix=${PREFIX} \
            --target=${TARGET} \
            --disable-nls \
            --disable-werror \
            --enable-languages=c \
            --without-docdir \
            --with-sysroot="$SYSROOT"

        make all-gcc all-target-libgcc -j $(nproc)
        make install-gcc install-target-libgcc
    popd
popd
