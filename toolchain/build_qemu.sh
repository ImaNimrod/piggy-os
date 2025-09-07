#!/bin/sh

set -eu

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$DIR/common.sh"

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    download_and_extract QEMU
popd

mkdir -p ${DIR}/build
pushd ${DIR}/build
    rm -rf build_*

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
