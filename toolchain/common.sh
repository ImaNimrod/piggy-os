UNAME=$(uname)

NPROC=1
if [ ${UNAME} == "Darwin" ]; then
    NPROC=$(sysctl -n hw.ncpu)
elif [ ${UNAME} == "Linux" ]; then
    NPROC=$(nproc)
else
    echo "trying to build toolchain on possibly unsupported host platform..."
fi

source "$DIR/toolchain.config"

export CFLAGS="-g0 -O2 -mtune=native -pipe"
export CXXFLAGS="-g0 -O2 -mtune=native -pipe"
export PATH="$PREFIX/bin:$PATH"

function download_and_extract() {
    declare -n NAME=${1^^}_NAME
    declare -n PKG=${1^^}_PKG
    declare -n BASE_URL=${1^^}_BASE_URL
    declare -n SHA256SUM=${1^^}_SHA256SUM

    if [ ! -d ${NAME} ]; then
        local sha256=""

        if [ -e ${PKG} ]; then
            sha256="$(sha256sum ${PKG} | cut -f1 -d ' ')"
        fi

        if [ "$sha256" != ${SHA256SUM} ] ; then
            rm -f ${PKG}
            echo "downloading ${PKG}..."
            curl -LO ${BASE_URL}/${PKG}

            sha256="$(sha256sum ${PKG} | cut -f1 -d ' ')"
            if [ "$sha256" != ${SHA256SUM} ] ; then
                echo "sha256sum comparision failed for ${PKG}"
                exit 1
            fi
        else
            echo "skipped downloading ${NAME}"
        fi

        echo "extracting ${NAME}..."
        tar -xf ${PKG}

        if [ -d "${DIR}/patches/${NAME}" ]; then
            echo "patching ${NAME}..."
            pushd "${DIR}/tarballs/${NAME}"
                for file in ${DIR}/patches/${NAME}/*.patch; do
                    patch -p1 < $file
                done
            popd
        fi
    else
        echo "using existing ${NAME} source"
    fi
}
