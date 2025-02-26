#!/bin/sh

NINJA_ROOT=$1
CMAKE_ROOT=$2

# Get version numbers
. $(dirname $0)/version_cmake.env

# Detect architecture
ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    CMAKE_ARCH="Linux-x86_64"
elif [ "$ARCH" = "aarch64" ]; then
    CMAKE_ARCH="Linux-aarch64"
else
    echo "Unsupported architecture: $ARCH"
    exit 1
fi

# Define download links
NINJA_URI=https://github.com/ninja-build/ninja/archive/refs/tags/${NINJA_VERSION}.tar.gz
CMAKE_URI=https://github.com/Kitware/CMake/releases/download/${CMAKE_VERSION}/cmake-${CMAKE_VERSION##v}-${CMAKE_ARCH}.sh

download () { 
    uri=$1
    tmp_file=$(basename $uri)

    if [ -z "$uri" ]; then
        echo "install_cmake.sh: download(): error: no uri provided"
        exit 1
    fi

    curl -SL $uri --output $tmp_file
    if [ $? -ne 0 ]; then
        echo "install_cmake.sh: download(): error: download failed ($uri)"
        exit 1
    fi

    uri_extension="${tmp_file##*.}"
    if [ "$uri_extension" = "gz" ]; then
        tar -xf $tmp_file
        if [ $? -ne 0 ]; then 
            echo "install_cmake.sh: download(): error: failed to unpack ($uri)"
            exit 1
        fi

        rm $tmp_file
    fi
}

# Download and build cmake
if [ "${CMAKE_ROOT}" != "" ]; then
    download ${CMAKE_URI}
    mv cmake*.sh cmake-install.sh
    chmod u+x cmake-install.sh
    mkdir ${CMAKE_ROOT}
    ./cmake-install.sh --skip-license --prefix=${CMAKE_ROOT}
fi

# Download and build ninja
download ${NINJA_URI}
mv ninja* ninja-source
cmake -Hninja-source -Bninja-source/build
cmake --build ninja-source/build
mkdir ${NINJA_ROOT}
mv ninja-source/build/ninja ${NINJA_ROOT}

# Clean up
rm -Rf ninja-source
if [ "${CMAKE_ROOT}" != "" ]; then
    rm -Rf cmake-install.sh
fi