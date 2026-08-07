#!/bin/bash

set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$DIR/common.sh"

mkdir -p "$DIR/tarballs"
pushd "$DIR/tarballs"
    download_and_extract AUTOCONF
    download_and_extract AUTOMAKE
popd

mkdir -p "$DIR/build_automake"
pushd "$DIR/build_automake"
    echo "configuring $AUTOMAKE_NAME..."

    "$DIR"/tarballs/"$AUTOMAKE_NAME"/configure \
        --prefix="$PREFIX"

    echo "building $AUTOMAKE_NAME..."

    make -j "$NPROC"
    make install
popd

rm -rf "$DIR/build_automake"

mkdir -p "$DIR/build_autoconf"
pushd "$DIR/build_autoconf"
    echo "configuring $AUTOCONF_NAME..."

    "$DIR"/tarballs/"$AUTOCONF_NAME"/configure \
        --prefix="$PREFIX"

    echo "building ${AUTOCONF_NAME}..."

    make -j "$NPROC"
    make install
popd

rm -rf "$DIR/build_autoconf"

