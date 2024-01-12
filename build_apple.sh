#!/bin/bash
TARGET_OS=$1
CONFIG=$2

echo "build start, os: ${TARGET_OS}"

if [ ! -d "external/${TARGET_OS}/ffmpeg" ] ; then
    FFPKG=ffmpeg-master-${TARGET_OS}-lite
    FFPKG_LOCAL=ffmpeg-${TARGET_OS}.tar.xz
    if [ ! -f "tmp/${FFPKG_LOCAL}" ] ; then
        if [ ! -d "tmp" ] ; then
            mkdir -p tmp
        fi
        echo "ffmpeg not found, download..."
        curl -kL -o tmp/${FFPKG_LOCAL} https://sourceforge.net/projects/avbuild/files/${TARGET_OS}/${FFPKG}.tar.xz/download
    fi
    mkdir -p external/${TARGET_OS}/ffmpeg
    tar Jxf tmp/${FFPKG_LOCAL} --strip-components=1 -C external/$TARGET_OS/ffmpeg
    echo "ffmpeg ready"
fi

if [ ! -d "external/${TARGET_OS}/thirdparty" ] ; then
    THPKG=thirdparty-${TARGET_OS}.tar.xz
    if [ ! -f "tmp/${THPKG}" ] ; then
        if [ ! -d "tmp" ] ; then
            mkdir -p tmp
        fi
        echo "thirdparty not found, download..."
        curl -kL -o tmp/${THPKG} https://sourceforge.net/projects/lite-obs-dep/files/${TARGET_OS}/${THPKG}/download
    fi
    mkdir -p external/${TARGET_OS}/thirdparty
    tar Jxf tmp/${THPKG} --strip-components=1 -C external/$TARGET_OS/thirdparty
    echo "thirdparty ready"
fi

echo "dep ready"

# cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 -GNinja -Bbuild/${TARGET_OS} -DCMAKE_VERBOSE_MAKEFILE=1 -DCMAKE_BUILD_TYPE=${CONFIG} -DCMAKE_INSTALL_PREFIX=$PWD/liteobs-sdk

cmake -GNinja -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/tools/ios.cmake -DIOS_DEPLOYMENT_TARGET=13.0 -DIOS_ARCH="arm64" -DIOS_BITCODE=0  -Bbuild/${TARGET_OS} -DCMAKE_BUILD_TYPE=${CONFIG} -DCMAKE_INSTALL_PREFIX=$PWD/liteobs-sdk -DCMAKE_VERBOSE_MAKEFILE=1

cmake --build build/${TARGET_OS}

cmake -P build/${TARGET_OS}/cmake_install.cmake