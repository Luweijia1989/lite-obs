NDK_HOST=linux
FF_EXTRA=-clang
FFPKG_EXT=tar.xz

echo "EXTERNAL_DEP_CACHE_HIT: ${EXTERNAL_DEP_CACHE_HIT}"
echo "DEVTOOLS_CACHE_HIT: ${DEVTOOLS_CACHE_HIT}"

du -hc external

if [[ "$TARGET_OS" == mac* || "$TARGET_OS" == iOS* || "$TARGET_OS" == android ]]; then
    FF_EXTRA=
fi
if [[ "$TARGET_OS" == "win"* || "$TARGET_OS" == "uwp"* ]]; then
  FF_EXTRA=-vs2022
  FFPKG_EXT=7z
fi

if [ `which dpkg` ]; then # TODO: multi arch
    pkgs="sshpass cmake ninja-build p7zip-full"
    #wget https://apt.llvm.org/llvm.sh
    if [[ "$TARGET_OS" != android ]]; then
        #bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)"
        wget https://apt.llvm.org/llvm.sh
        chmod +x llvm.sh
        sudo ./llvm.sh ${LLVM_VER} all
    fi
    sudo apt install -y $pkgs
elif [ `which brew` ]; then
    #time brew update --preinstall
    export HOMEBREW_NO_AUTO_UPDATE=1
    pkgs="p7zip ninja" #
    if [[ "$DEVTOOLS_CACHE_HIT" != "true" ]]; then
        pkgs+=" hudochenkov/sshpass/sshpass"
    fi
    time brew install $pkgs
    NDK_HOST=darwin
fi

mkdir -p external/${TARGET_OS}/{ffmpeg,thirdparty}

if [[ "$EXTERNAL_DEP_CACHE_HIT" != "true" ]]; then
  FFPKG=ffmpeg-${FF_VER}-${TARGET_OS}${FF_EXTRA}-lite
  curl -kL -o ffmpeg-${TARGET_OS}.${FFPKG_EXT} https://sourceforge.net/projects/avbuild/files/${TARGET_OS}/${FFPKG}.${FFPKG_EXT}/download
  if [[ "${FFPKG_EXT}" == 7z ]]; then
    rm external/${TARGET_OS}/ffmpeg
    7z x ffmpeg-${TARGET_OS}.${FFPKG_EXT} -o./external/${TARGET_OS}
    mv external/${TARGET_OS}/${FFPKG} external/${TARGET_OS}/ffmpeg
  else
    tar Jxf ffmpeg-${TARGET_OS}.${FFPKG_EXT} --strip-components=1 -C external/$TARGET_OS/ffmpeg
  fi

  curl -kL -o thirdparty-${TARGET_OS}.${FFPKG_EXT} https://sourceforge.net/projects/lite-obs-dep/files/${TARGET_OS}/thirdparty-${TARGET_OS}.${FFPKG_EXT}/download
  if [[ "${FFPKG_EXT}" == 7z ]]; then
    rm external/${TARGET_OS}/thirdparty
    7z x thirdparty-${TARGET_OS}.${FFPKG_EXT} -o./external/${TARGET_OS}
    mv external/${TARGET_OS}/thirdparty-${TARGET_OS} external/${TARGET_OS}/thirdparty
  else
    tar Jxf thirdparty-${TARGET_OS}.${FFPKG_EXT} --strip-components=1 -C external/$TARGET_OS/thirdparty
  fi
fi

if [[ "$SYSROOT_CACHE_HIT" != "true" ]]; then
  if [ "$TARGET_OS" == "android" -a ! -d "$ANDROID_NDK_LATEST_HOME" ]; then
    wget https://dl.google.com/android/repository/android-ndk-${NDK_VERSION:-r25b}-${NDK_HOST}-x86_64.zip -O ndk.zip
    7z x ndk.zip -o/tmp &>/dev/null
    mv /tmp/android-ndk-${NDK_VERSION:-r25b} ${ANDROID_NDK:-/tmp/android-ndk}
  fi
fi
