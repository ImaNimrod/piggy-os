#!/bin/bash

set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$DIR/common.sh"

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    download_and_extract QEMU
popd

mkdir -p "$DIR/build_qemu"
pushd "$DIR/build_qemu"
    echo "configuring ${QEMU_NAME}..."

    GUI_ARGS=""
    if [ ${UNAME} == "Darwin" ]; then
        GUI_ARGS="--enable-cocoa --disable-sdl"
    else
        GUI_ARGS="--enable-gtk --enable-vte"
    fi

    ${DIR}/tarballs/${QEMU_NAME}/configure \
        --prefix=${PREFIX} \
        --target-list=x86_64-softmmu \
        --disable-werror \
        --enable-slirp \
        $GUI_ARGS || exit 1

    echo "building ${QEMU_NAME}..."

    make -j $NPROC || exit 1
    make install || exit 1
popd

rm -rf "$DIR/build_qemu"
