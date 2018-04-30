#!/bin/bash
# This file was generated by zenbuild,
# with the following command:
# ./make-extra.sh x264 ffmpeg gpac libsdl2 
#
# This script builds the following packages:
# - expat
# - faad2
# - ffmpeg
# - fontconfig
# - freetype2
# - fribidi
# - gpac
# - liba52
# - libass
# - libjpeg
# - libmad
# - libogg
# - libpng
# - libpthread
# - librtmp
# - libsdl2
# - libtheora
# - libvorbis
# - libxvidcore
# - opencore-amr
# - x264
# - zlib
#


function expat_build {
  local host=$1
  pushDir $WORK/src

  lazy_download "expat.tar.xz" "http://sourceforge.net/projects/expat/files/expat/2.1.0/expat-2.1.0.tar.gz/download"
  lazy_extract "expat.tar.xz"
  mkgit "expat"

  CFLAGS+=" -I$PREFIX/$host/include " \
  LDFLAGS+=" -L$PREFIX/$host/lib " \
  autoconf_build $host "expat" \
    --enable-shared \
    --disable-static

  popDir
}

function expat_get_deps {
  local a=0
}


function faad2_build {
  host=$1
  pushDir $WORK/src

  lazy_download "faad2.tar.bz2" "http://downloads.sourceforge.net/faac/faad2-2.7.tar.bz2"
  lazy_extract "faad2.tar.bz2"
  mkgit "faad2"
  
  pushDir "faad2"
  faad2_patches
  popDir

  autoconf_build $host "faad2"

  popDir
}

function faad2_get_deps {
  echo ""
}

function faad2_patches {
  local patchFile=$scriptDir/patches/faad2_01_ErrorDeclSpecifier.diff
  cat << 'EOF' > $patchFile
diff --git a/frontend/main.c b/frontend/main.c
--- a/frontend/main.c
+++ b/frontend/main.c
@@ -31,7 +31,9 @@
 #ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
+#ifndef __MINGW32__
 #define off_t __int64
+#endif
 #else
 #include <time.h>
 #endif
EOF

  applyPatch $patchFile
}


function ffmpeg_build {
  host=$1
  pushDir $WORK/src

  lazy_git_clone git://source.ffmpeg.org/ffmpeg.git ffmpeg 4588063f3ecd9

  local ARCH=$(get_arch $host)
  local OS=$(get_os $host)

  local os=$OS
  case $OS in
    darwin*)
      os="darwin"
      ;;
  esac

  # remove stupid dependency
  $sed -i "s/jack_jack_h pthreads/jack_jack_h/" ffmpeg/configure
  
  mkdir -p ffmpeg/build/$host
  pushDir ffmpeg/build/$host

  LDFLAGS="-L$PREFIX/$host/lib -lz" \
  ../../configure \
      --prefix=$PREFIX/$host \
      --enable-pthreads \
      --disable-w32threads \
      --disable-debug \
      --disable-static \
      --enable-shared \
      --enable-libass \
      --enable-fontconfig \
      --enable-librtmp \
      --enable-gpl \
      --enable-libx264 \
      --enable-zlib \
      --disable-gnutls \
      --disable-openssl \
      --disable-gnutls \
      --disable-openssl \
      --disable-iconv \
      --disable-bzlib \
      --enable-avresample \
      --pkg-config=pkg-config \
      --target-os=$os \
      --arch=$ARCH \
      --cross-prefix=$host-
  $MAKE
  $MAKE install
  popDir

  popDir
}

function ffmpeg_get_deps {
  echo libass
  echo fontconfig
  echo librtmp
  echo libpthread
  echo x264
  echo zlib
}


function fontconfig_build {
  host=$1
  pushDir $WORK/src

  lazy_download "fontconfig.tar.bz2" "http://www.freedesktop.org/software/fontconfig/release/fontconfig-2.12.6.tar.bz2"
  lazy_extract "fontconfig.tar.bz2"
  mkgit "fontconfig"
  pushDir fontconfig

  autoreconf -fiv

  mkdir -p build/$host
  pushDir build/$host
  ../../configure \
    --build=$BUILD \
    --host=$host \
    --prefix=$PREFIX/$host \
    --enable-shared \
    --disable-static
  $MAKE
  $MAKE install || true #avoid error: /usr/bin/install: cannot stat
  popDir

  popDir
  popDir
}

function fontconfig_get_deps {
  echo "expat"
  echo "freetype2"
}


function freetype2_build {
  host=$1
  pushDir $WORK/src

  lazy_download "freetype2.tar.bz2" "http://download.savannah.gnu.org/releases/freetype/freetype-2.7.1.tar.bz2"
  lazy_extract "freetype2.tar.bz2"
  mkgit "freetype2"

  pushDir "freetype2"
  freetype2_patches
  popDir

  autoconf_build $host "freetype2" \
    "--without-png" \
    "--enable-shared" \
    "--disable-static"

  popDir
}

function freetype2_get_deps {
  echo zlib
}

function freetype2_patches {
  local patchFile=$scriptDir/patches/freetype2_01_pkgconfig.diff
  cat << 'EOF' > $patchFile
diff --git a/builds/unix/freetype2.in b/builds/unix/freetype2.in
index c4dfda4..97f256e 100644
--- a/builds/unix/freetype2.in
+++ b/builds/unix/freetype2.in
@@ -7,7 +7,7 @@ Name: FreeType 2
 URL: http://freetype.org
 Description: A free, high-quality, and portable font engine.
 Version: %ft_version%
-Requires:
+Requires: %REQUIRES_PRIVATE%
 Requires.private: %REQUIRES_PRIVATE%
 Libs: -L${libdir} -lfreetype
 Libs.private: %LIBS_PRIVATE%
EOF

  applyPatch $patchFile
}



function fribidi_build {
  host=$1
  pushDir $WORK/src

  lazy_download "fribidi.tar.xz" "https://ftp.osuosl.org/pub/blfs/conglomeration/fribidi/fribidi-0.19.6.tar.bz2"
  lazy_extract "fribidi.tar.xz"
  mkgit "fribidi"
  
  pushDir "fribidi"
  fribidi_patches
  popDir

  autoconf_build $host "fribidi" \
    --enable-shared \
    --disable-static

  popDir
}

function fribidi_get_deps {
  local a=0
}

function fribidi_patches {
  local patchFile=$scriptDir/patches/fribidi_01_ExportSymbolsDup.diff
  cat << 'EOF' > $patchFile
diff -urN lib/Makefile.am fribidi/lib/Makefile.am
--- a/lib/Makefile.am	2012-12-30 00:11:33.000000000 +0000
+++ b/lib/Makefile.am	2014-07-26 16:01:01.785635600 +0100
@@ -7,12 +7,7 @@
 libfribidi_la_LDFLAGS = -no-undefined -version-info $(LT_VERSION_INFO)
 libfribidi_la_LIBADD = $(MISC_LIBS)
 libfribidi_la_DEPENDENCIES =
-
-if OS_WIN32
-libfribidi_la_LDFLAGS += -export-symbols $(srcdir)/fribidi.def
-else
 libfribidi_la_LDFLAGS += -export-symbols-regex "^fribidi_.*"
-endif # OS_WIN32
 
 if FRIBIDI_CHARSETS
 
diff -urN fribidi/lib/Makefile.in fribidi-0.19.6/lib/Makefile.in
--- a/lib/Makefile.in	2013-12-06 20:56:59.000000000 +0000
+++ b/lib/Makefile.in	2014-07-26 16:03:34.860073600 +0100
@@ -79,11 +79,9 @@
 POST_UNINSTALL = :
 build_triplet = @build@
 host_triplet = @host@
-@OS_WIN32_TRUE@am__append_1 = -export-symbols $(srcdir)/fribidi.def
-@OS_WIN32_FALSE@am__append_2 = -export-symbols-regex "^fribidi_.*"
-@FRIBIDI_CHARSETS_TRUE@am__append_3 = -I$(top_srcdir)/charset
-@FRIBIDI_CHARSETS_TRUE@am__append_4 = $(top_builddir)/charset/libfribidi-char-sets.la
-@FRIBIDI_CHARSETS_TRUE@am__append_5 = $(top_builddir)/charset/libfribidi-char-sets.la
+@FRIBIDI_CHARSETS_TRUE@am__append_1 = -I$(top_srcdir)/charset
+@FRIBIDI_CHARSETS_TRUE@am__append_2 = $(top_builddir)/charset/libfribidi-char-sets.la
+@FRIBIDI_CHARSETS_TRUE@am__append_3 = $(top_builddir)/charset/libfribidi-char-sets.la
 DIST_COMMON = $(srcdir)/Headers.mk $(srcdir)/Makefile.in \
 	$(srcdir)/Makefile.am $(srcdir)/fribidi-config.h.in \
 	$(top_srcdir)/depcomp $(pkginclude_HEADERS)
@@ -338,11 +336,11 @@
 top_srcdir = @top_srcdir@
 EXTRA_DIST = fribidi.def
 lib_LTLIBRARIES = libfribidi.la
-AM_CPPFLAGS = $(MISC_CFLAGS) $(am__append_3)
+AM_CPPFLAGS = $(MISC_CFLAGS) $(am__append_1)
 libfribidi_la_LDFLAGS = -no-undefined -version-info $(LT_VERSION_INFO) \
-	$(am__append_1) $(am__append_2)
-libfribidi_la_LIBADD = $(MISC_LIBS) $(am__append_4)
-libfribidi_la_DEPENDENCIES = $(am__append_5)
+	-export-symbols-regex "^fribidi_.*"
+libfribidi_la_LIBADD = $(MISC_LIBS) $(am__append_2)
+libfribidi_la_DEPENDENCIES = $(am__append_3)
 libfribidi_la_headers = \
 		fribidi-arabic.h \
 		fribidi-begindecls.h \
EOF

  applyPatch $patchFile
}


function gpac_build {
  host=$1
  pushDir $WORK/src

  lazy_git_clone https://github.com/gpac/gpac.git gpac e0ac0849fea

  local OS=$(get_os $host)
  local crossPrefix=$(get_cross_prefix $BUILD $host)

  local os=$OS
  case $OS in
    darwin*)
      os="Darwin"
      ;;
  esac

  mkdir -p gpac/build/$host
  pushDir gpac/build/$host

  ../../configure \
    --target-os=$os \
    --cross-prefix="$crossPrefix" \
    --extra-cflags="-I$PREFIX/$host/include -w -fPIC" \
    --extra-ldflags="-L$PREFIX/$host/lib -Wl,-rpath-link=$PREFIX/$host/lib" \
    --sdl-cfg=":$PREFIX/$host/bin" \
    --disable-jack \
    --enable-amr \
    --prefix=$PREFIX/$host

  $MAKE
  $MAKE install
  $MAKE install-lib

  popDir
  popDir
}

function gpac_get_deps {
  echo libpthread
  echo faad2
  echo ffmpeg
  echo freetype2
  echo liba52
  echo libjpeg
  echo libmad
  echo libogg
  echo libpng
  echo libsdl2
  echo libtheora
  echo libvorbis
  echo libxvidcore
  echo libogg
  echo opencore-amr
  echo zlib
}


function liba52_build {
  host=$1
  pushDir $WORK/src

  lazy_download "liba52.tar.xz" "http://liba52.sourceforge.net/files/a52dec-0.7.4.tar.gz"
  lazy_extract "liba52.tar.xz"

  CFLAGS="-w -fPIC -std=gnu89" \
  autoconf_build $host "liba52" \
    --enable-shared \
    --disable-static
  
  popDir
}

function liba52_get_deps {
  local a=0
}


function libass_build {
  host=$1
  pushDir $WORK/src

  lazy_download "libass.tar.xz" "https://github.com/libass/libass/releases/download/0.13.2/libass-0.13.2.tar.xz"
  lazy_extract "libass.tar.xz"
  mkgit "libass"

  autoconf_build $host "libass" \
    --enable-shared \
    --disable-static

  popDir
}

function libass_get_deps {
  echo freetype2
  echo fribidi
  echo fontconfig
}


function libjpeg_get_deps {
  local a=0
}

function libjpeg_build {
  local host=$1

  pushDir $WORK/src
  lazy_download "libjpeg-$host.tar.gz" "http://www.ijg.org/files/jpegsrc.v9a.tar.gz"

  lazy_extract "libjpeg-$host.tar.gz"

  autoconf_build $host "libjpeg-$host" \
    --enable-dependency-tracking

  popDir
}


function libmad_get_deps {
  local a=0
}

function libmad_build {
  local host=$1
  pushDir $WORK/src

  lazy_download "libmad.tar.gz" "http://sourceforge.net/projects/mad/files/libmad/0.15.1b/libmad-0.15.1b.tar.gz"
  lazy_extract "libmad.tar.gz"
  
  if [ $(uname -s) == "Darwin" ]; then
    $sed -i "s/-fforce-mem//" libmad/configure
    $sed -i "s/-fthread-jumps//" libmad/configure
    $sed -i "s/-fcse-follow-jumps//" libmad/configure
    $sed -i "s/-fcse-skip-blocks//" libmad/configure
    $sed -i "s/-fregmove//" libmad/configure
    $sed -i "s/-march=i486//" libmad/configure
  else
    $sed -i "s/-fforce-mem//" libmad/configure
  fi

  autoconf_build $host "libmad"

  popDir
}


function libogg_build {
  host=$1
  pushDir $WORK/src

  lazy_download "libogg.tar.gz" "http://downloads.xiph.org/releases/ogg/libogg-1.3.1.tar.gz"
  lazy_extract "libogg.tar.gz"
  mkgit "libogg"

  autoconf_build $host "libogg"
  popDir
}

function libogg_get_deps {
  echo ""
}


function libpng_build {
  host=$1
  pushDir $WORK/src

  lazy_download "libpng.tar.xz" "http://prdownloads.sourceforge.net/libpng/libpng-1.2.52.tar.xz?download"
  lazy_extract "libpng.tar.xz"
  mkgit "libpng"

  LDFLAGS+=" -L$WORK/release/$host/lib" \
  CFLAGS+=" -I$WORK/release/$host/include" \
  CPPFLAGS+=" -I$WORK/release/$host/include" \
  autoconf_build $host "libpng" \
    --enable-shared \
    --disable-static

  popDir
}

function libpng_get_deps {
  echo "zlib"
}


function libpthread_get_deps {
  local a=0
}

function libpthread_build_mingw {
  local host=$1
  pushDir $WORK/src
  
  lazy_download "mingw-w64.tar.bz2" "http://sourceforge.net/projects/mingw-w64/files/mingw-w64/mingw-w64-release/mingw-w64-v3.1.0.tar.bz2/download"
  lazy_extract "mingw-w64.tar.bz2"
  pushDir mingw-w64/mingw-w64-libraries
  
  autoconf_build $host "winpthreads"
 
  rm $PREFIX/$host/include/pthread.h #remove broken header - will fallback on the pthread correct one
 
  popDir
  popDir
}

function libpthread_build_native {
  local host=$1
  pushDir $WORK/src

  lazy_download "libpthread.tar.bz2" "http://xcb.freedesktop.org/dist/libpthread-stubs-0.1.tar.bz2"
  lazy_extract "libpthread.tar.bz2"
  autoconf_build $host "libpthread"

  popDir
}

function libpthread_build {
  case $host in
    *mingw*)
      libpthread_build_mingw $@
      ;;
    *)
      libpthread_build_native $@
      ;;
  esac
}


function librtmp_get_deps {
  local a=0
}

function librtmp_build {
  host=$1
  pushDir $WORK/src
  lazy_git_clone "git://git.ffmpeg.org/rtmpdump" rtmpdump 79459a2b43f41ac44a2ec001139bcb7b1b8f7497

  pushDir rtmpdump/librtmp
  if [ $(uname -s) == "Darwin" ]; then
    librtmp_patches
  fi

  case $host in
    *mingw*)
      $sed -i "s/^SYS=posix/SYS=mingw/" Makefile
      echo "# YO" >> Makefile
      ;;
  esac

  $sed -i "s@^prefix=.*@prefix=$PREFIX/$host@" Makefile
  $sed -i "s@^CRYPTO=.*@@" Makefile

  $MAKE CROSS_COMPILE="$host-"
  $MAKE CROSS_COMPILE="$host-" install

  popDir
  popDir
}

function librtmp_patches {
  local patchFile=$scriptDir/patches/librtmp_01_dylib_install_name.diff
  cat << 'EOF' > $patchFile
--- a/Makefile	2015-09-09 13:29:23.000000000 +0200
+++ b/Makefile	2015-09-09 13:30:34.000000000 +0200
@@ -53,7 +53,7 @@
 SODIR_mingw=$(BINDIR)
 SODIR=$(SODIR_$(SYS))
 
-SO_LDFLAGS_posix=-shared -Wl,-soname,$@
+SO_LDFLAGS_posix=-shared -Wl,-dylib_install_name,$@
 SO_LDFLAGS_darwin=-dynamiclib -twolevel_namespace -undefined dynamic_lookup \
 	-fno-common -headerpad_max_install_names -install_name $(libdir)/$@
 SO_LDFLAGS_mingw=-shared -Wl,--out-implib,librtmp.dll.a
EOF

  applyPatch $patchFile
}


function libsdl2_get_deps {
  local a=0
}

function libsdl2_build {
  local host=$1
  pushDir $WORK/src

  lazy_download "libsdl2.tar.gz" "https://www.libsdl.org/release/SDL2-2.0.6.tar.gz"
  lazy_extract "libsdl2.tar.gz" 
  mkgit "libsdl2"

  autoconf_build $host "libsdl2"

  popDir
}


function libtheora_build {
  host=$1
  pushDir $WORK/src

  lazy_download "libtheora.tar.bz2" "http://downloads.xiph.org/releases/theora/libtheora-1.1.1.tar.bz2"
  lazy_extract "libtheora.tar.bz2"
  mkgit "libtheora"

  mkdir -p libtheora/build/$host
  pushDir libtheora/build/$host

  ../../configure \
    --build=$BUILD \
    --host=$host \
    --prefix=$PREFIX/$host \
    --enable-shared \
    --disable-static \
    --disable-examples
  $MAKE || true
  $sed -i 's/\(1q \\$export_symbols\)/\1|tr -d \\\\\\\"\\r\\\\\\\"/' libtool
  $MAKE
  $MAKE install

  popDir
  popDir
}

function libtheora_get_deps {
  echo "libogg"
}


function libvorbis_build {
  host=$1
  pushDir $WORK/src

  lazy_download "libvorbis.tar.gz" "http://downloads.xiph.org/releases/vorbis/libvorbis-1.3.4.tar.gz"
  lazy_extract "libvorbis.tar.gz"
  mkgit "libvorbis"

  autoconf_build $host "libvorbis"

  popDir
}

function libvorbis_get_deps {
  echo "libogg"
}


function libxvidcore_get_deps {
  local a=0
}

function libxvidcore_build {
  local host=$1
  pushDir $WORK/src

  lazy_download "libxvidcore.tar.gz" "http://downloads.xvid.org/downloads/xvidcore-1.3.3.tar.gz" 
  lazy_extract "libxvidcore.tar.gz"
  
  pushDir libxvidcore/build/generic
  
  cp $scriptDir/config.guess .
  ./configure \
     --host=$host \
     --prefix=$PREFIX/$host
  
  $MAKE
  $MAKE install 
 

  popDir 
  popDir
}


function opencore-amr_get_deps {
  local a=0
}

function opencore-amr_build {
  local host=$1
  pushDir $WORK/src

  lazy_download "opencore-amr.tar.gz" "http://sourceforge.net/projects/opencore-amr/files/opencore-amr/opencore-amr-0.1.3.tar.gz"
  lazy_extract "opencore-amr.tar.gz"

  autoconf_build $host "opencore-amr" --enable-shared

  popDir
} 


function x264_build {
  local host=$1
  local crossPrefix=$(get_cross_prefix $BUILD $host)

  pushDir $WORK/src
  lazy_git_clone "git://git.videolan.org/x264.git" x264 40bb56814e56ed342040bdbf30258aab39ee9e89

  local build="autoconf_build $host x264 \
    --enable-static \
    --enable-pic \
    --disable-gpl \
    --disable-cli \
    $THREADING \
    --enable-strip \
    --disable-avs \
    --disable-swscale \
    --disable-lavf \
    --disable-ffms \
    --disable-gpac \
    --disable-opencl \
    --cross-prefix=$crossPrefix"
  case $host in
    *darwin*)
      RANLIB="" $build
      ;;
    *mingw*)
      THREADING="--enable-win32thread" $build
      ;;
    *)
      $build
      ;;
  esac

  popDir
}

function x264_get_deps {
  echo "libpthread"
}


function zlib_get_deps {
  local a=0
}

function zlib_build {
  host=$1
  pushDir $WORK/src
  lazy_download "zlib-$host.tar.gz" "http://zlib.net/fossils/zlib-1.2.9.tar.gz"
  lazy_extract "zlib-$host.tar.gz"
  mkgit "zlib-$host"
  pushDir zlib-$host

  zlib_patches
  
  chmod +x ./configure
  CFLAGS="-w -fPIC" \
  CHOST=$host \
    ./configure \
    --prefix=$PREFIX/$host
  $MAKE
  $MAKE install
  
  popDir
  popDir
}

function zlib_patches {
  local patchFile=$scriptDir/patches/zlib_01_nobypass.diff
  cat << 'EOF' > $patchFile
diff --git a/configure b/configure
index b77a8a8..c82aea1 100644
--- a/configure
+++ b/configure
@@ -191,10 +191,6 @@ if test "$gcc" -eq 1 && ($cc -c $test.c) >> configure.log 2>&1; then
   CYGWIN* | Cygwin* | cygwin* | OS/2*)
         EXE='.exe' ;;
   MINGW* | mingw*)
-# temporary bypass
-        rm -f $test.[co] $test $test$shared_ext
-        echo "Please use win32/Makefile.gcc instead." | tee -a configure.log
-        leave 1
         LDSHARED=${LDSHARED-"$cc -shared"}
         LDSHAREDLIBC=""
         EXE='.exe' ;;
EOF

  applyPatch $patchFile
}
#####################################
# ZenBuild utility functions
#####################################


set -e

function prefixLog {
  pfx=$1
  shift
  "$@" 2>&1 | sed -u "s/^.*$/$pfx&/"
}

function printMsg {
  echo -n "[32m"
  echo $*
  echo -n "[0m"
}

function isMissing {
  progName=$1
  echo -n "Checking for $progName ... "
  if which $progName 1>/dev/null 2>/dev/null; then
    echo "ok"
    return 1
  else
    return 0
  fi
}

function installErrorHandler {
  trap "printMsg 'Spawning a rescue shell in current build directory'; PS1='\\w: rescue$ ' bash --norc" EXIT
}

function uninstallErrorHandler {
  trap - EXIT
}

function lazy_download {
  local file="$1"
  local url="$2"

  if [ ! -e "$CACHE/$file" ]; then
    echo "Downloading: $file"
    wget "$url" -c -O "$CACHE/${file}.tmp" --no-verbose
    mv "$CACHE/${file}.tmp" "$CACHE/$file"
  fi
}

function lazy_extract {
  local archive="$1"
  echo -n "Extracting $archive ... "
  local name=$(basename $archive .tar.gz)
  name=$(basename $name .tar.bz2)
  name=$(basename $name .tar.xz)

  local tar_cmd="tar"
  if [ $(uname -s) == "Darwin" ]; then
    tar_cmd="gtar"
  fi

  if [ -d $name ]; then
    echo "already extracted"
  else
    rm -rf ${name}.tmp
    mkdir ${name}.tmp
    $tar_cmd -C ${name}.tmp -xlf "$CACHE/$archive"  --strip-components=1
    mv ${name}.tmp $name
    echo "ok"
  fi
}

function lazy_git_clone {
  local url="$1"
  local to="$2"
  local rev="$3"

  if [ -d "$to" ] ;
  then
    pushDir "$to"
    git reset -q --hard
    git clean -q -f
    popDir
  else
    git clone "$url" "$to"
  fi

  pushDir "$to"
  git checkout -q $rev
  popDir
}

function mkgit {
  dir="$1"

  pushDir "$dir"
  if [ -d ".git" ]; then
    printMsg "Restoring $dir from git restore point"
    git reset -q --hard
    git clean -q -f
  else
    printMsg "Creating git for $dir"
    git init
    git config user.email "nobody@localhost"
    git config user.name "Nobody"
    git config core.autocrlf false
    git add -f *
    git commit -m "MinGW/GDC restore point"
  fi
  popDir
}

function applyPatch {
  local patchFile=$1
  printMsg "Patching $patchFile"
  if [ $(uname -s) == "Darwin" ]; then 
    patch  --no-backup-if-mismatch -p1 -i $patchFile
  else
    patch  --no-backup-if-mismatch --merge -p1 -i $patchFile
  fi
}

function main {
  local packageName=$2
  local hostPlatform=$3

  if [ -z "$1" ] || [ -z "$packageName" ] || [ -z "$hostPlatform" ] ; then
    echo "Usage: $0 <workDir> <packageName> <hostPlatform|->"
    echo "Example: $0 /tmp/work libav x86_64-w64-mingw32"
    echo "Example: $0 /tmp/work gpac -"
    exit 1
  fi

  mkdir -p "$1"
  WORK=$(get_abs_dir "$1")

  if echo $PATH | grep " " ; then
    echo "Your PATH contain spaces, this may cause build issues."
    echo "Please clean-up your PATH and retry."
    echo "Example:"
    echo "$ PATH=/mingw64/bin:/bin:/usr/bin ./zenbuild.sh <options>"
    exit 3
  fi

  if [ ! -e "$scriptDir/config.guess" ]; then
    wget -O "$scriptDir/config.guess" 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.guess;hb=HEAD'
    chmod +x "$scriptDir/config.guess"
  fi
  BUILD=$($scriptDir/config.guess | sed 's/-unknown//' | sed 's/-msys$/-mingw32/')
  mkdir -p patches

  printMsg "Building in: $WORK"

  printMsg "Build platform: $BUILD"
  printMsg "Target platform: $hostPlatform"

  if [ $hostPlatform = "-" ]; then
    hostPlatform=$BUILD
  fi

  initSymlinks
  checkForCrossChain "$BUILD" "$hostPlatform"
  checkForCommonBuildTools

  CACHE=$WORK/cache
  mkdir -p $CACHE
  mkdir -p $WORK/src

  export PREFIX="$WORK/release"
  for dir in "lib" "bin" "include" 
  do
    mkdir -p "$PREFIX/${hostPlatform}/${dir}"
  done

  initCflags
  installErrorHandler

  build ${hostPlatform} ${packageName}

  uninstallErrorHandler
}

function initSymlinks {
  local symlink_dir=$WORK/symlinks
  mkdir -p $symlink_dir
  local tools="gcc g++ ar as nm strings strip"
  case $hostPlatform in
   # *darwin*)
   #   echo "Detected new Darwin host ($host): disabling ranlib"
   #   ;;
    *)
      tools="$tools ranlib"
      ;;
  esac
  for tool in $tools
  do
    local dest=$symlink_dir/$hostPlatform-$tool
    if [ ! -f $dest ]; then
      ln -s $(which $tool) $dest
    fi
  done
  export PATH=$PATH:$symlink_dir
}

function initCflags {
  # avoid interferences from environment
  unset CC
  unset CXX
  unset CFLAGS
  unset CXXFLAGS
  unset LDFLAGS

  CFLAGS="-O2"
  CXXFLAGS="-O2"
  LDFLAGS="-s"

  CFLAGS+=" -w"
  CXXFLAGS+=" -w"

  if [ $(uname -s) != "Darwin" ]; then
    LDFLAGS+=" -static-libgcc"
    LDFLAGS+=" -static-libstdc++"
  fi

  export CFLAGS
  export CXXFLAGS
  export LDFLAGS

  if [ $(uname -s) == "Darwin" ]; then
    local cores=$(sysctl -n hw.logicalcpu)
  else
    local cores=$(nproc)
  fi

  if [ -z "$MAKE" ]; then
    MAKE="make -j$cores"
  fi

  export MAKE
}

function importPkgScript {
  local name=$1
  if ! test -f zen-${name}.sh; then
    echo "Package $name does not have a zenbuild script"
    exit 1
  fi

  source zen-${name}.sh
}

function lazy_build {
  local host=$1
  local name=$2

  export PKG_CONFIG_PATH=$PREFIX/$host/lib/pkgconfig
  export PKG_CONFIG_LIBDIR=$PREFIX/$host/lib/pkgconfig

  if is_built $host $name ; then
    printMsg "$name: already built"
    return
  fi


  printMsg "$name: building ..."

  local deps=$(${name}_get_deps)
  for depName in $deps ; do
    build $host $depName
  done

  ${name}_build $host

  printMsg "$name: build OK"
  mark_as_built $host $name
}

function mark_as_built {
  local host=$1
  local name=$2

  local flagfile="$WORK/flags/$host/${name}.built"
  mkdir -p $(dirname $flagfile)
  touch $flagfile
}

function is_built {
  local host=$1
  local name=$2

  local flagfile="$WORK/flags/$host/${name}.built"
  if [ -f "$flagfile" ] ;
  then
    return 0
  else
    return 1
  fi
}

function autoconf_build {
  local host=$1
  shift
  local name=$1
  shift

  if [ ! -f $name/configure ] ; then
    printMsg "WARNING: package '$name' has no configure script, running autoreconf"
    pushDir $name
    aclocal && libtoolize --force && autoreconf
    popDir
  fi
  
  rm -rf $name/build/$host
  mkdir -p $name/build/$host
  pushDir $name/build/$host
  ../../configure \
    --build=$BUILD \
    --host=$host \
    --prefix=$PREFIX/$host \
    "$@"
  $MAKE
  $MAKE install
  popDir
}

function build {
  local host=$1
  local name=$2
    lazy_build $host $name
}

function pushDir {
  local dir="$1"
  pushd "$dir" 1>/dev/null 2>/dev/null
}

function popDir {
  popd 1>/dev/null 2>/dev/null
}

function get_cross_prefix {
  local build=$1
  local host=$2

  if [ "$host" = "-" ] ; then
    echo ""
  else
    echo "$host-"
  fi
}

function checkForCrossChain {
  local build=$1
  local host=$2
  local error="0"

  local cross_prefix=$(get_cross_prefix $build $host)

  # ------------- GCC -------------
  if isMissing "${cross_prefix}g++" ; then
    echo "No ${cross_prefix}g++ was found in the PATH."
    error="1"
  fi

  if isMissing "${cross_prefix}gcc" ; then
    echo "No ${cross_prefix}gcc was found in the PATH."
    error="1"
  fi

  # ------------- Binutils -------------
  if isMissing "${cross_prefix}nm" ; then
    echo "No ${cross_prefix}nm was found in the PATH."
    error="1"
  fi

  if isMissing "${cross_prefix}ar" ; then
    echo "No ${cross_prefix}ar was found in the PATH."
    error="1"
  fi

  if [ $(uname -s) != "Darwin" ]; then
    if isMissing "${cross_prefix}ranlib" ; then
      echo "No ${cross_prefix}ranlib was found in the PATH."
       error="1"
    fi
  fi

  if isMissing "${cross_prefix}strip" ; then
    echo "No ${cross_prefix}strip was found in the PATH."
    error="1"
  fi

  if isMissing "${cross_prefix}strings" ; then
    echo "No ${cross_prefix}strings was found in the PATH."
    error="1"
  fi

  if isMissing "${cross_prefix}as" ; then
    echo "No ${cross_prefix}as was found in the PATH."
    error="1"
  fi

  local os=$(get_os "$host")
  if [ $os == "mingw32" ] ; then
    if isMissing "${cross_prefix}dlltool" ; then
      echo "No ${cross_prefix}dlltool was found in the PATH."
      error="1"
    fi

    if isMissing "${cross_prefix}windres" ; then
      echo "No ${cross_prefix}windres was found in the PATH."
      error="1"
    fi
  fi

  if [ $error == "1" ] ; then
    exit 1
  fi
}

function checkForCommonBuildTools {
  local error="0"

  if isMissing "pkg-config"; then
    echo "pkg-config not installed.  Please install with:"
    echo "pacman -S pkgconfig"
    echo "or"
    echo "apt-get install pkg-config"
    echo ""
    error="1"
  fi

  if isMissing "patch"; then
    echo "patch not installed.  Please install with:"
    echo "pacman -S patch"
    echo "or"
    echo "apt-get install patch"
    echo ""
    error="1"
  fi

  if isMissing "python2"; then
    echo "python2 not installed.  Please install with:"
    echo "pacman -S python2"
    echo "or"
    echo "apt-get install python2"
    echo "or"
    echo "port install python27 && ln -s /opt/local/bin/python2.7 /opt/local/bin/python2"
    echo ""
    error="1"
  fi

  if isMissing "autoreconf"; then
    echo "autoreconf not installed. Please install with:"
    echo "pacman -S autoconf"
    echo "or"
    echo "apt-get install autoconf"
    echo "or"
    echo "port install autoconf"
    echo ""
    error="1"
    exit 1
  fi

  if isMissing "aclocal"; then
    echo "aclocal not installed. Please install with:"
    echo "pacman -S automake"
    echo "or"
    echo "apt-get install automake"
    echo "or"
    echo "port install automake"
    echo ""
    error="1"
    exit 1
  fi

  if isMissing "libtool"; then
    echo "libtool not installed.  Please install with:"
    echo "pacman -S msys/libtool"
    echo "or"
    echo "apt-get install libtool libtool-bin"
    echo ""
    error="1"
  fi
  
  # We still need to check that on Mac OS
  if [ $(uname -s) == "Darwin" ]; then
    if isMissing "glibtool"; then
      echo "libtool is not installed. Please install with:"
      echo "brew install libtool"
      echo "or"
      echo "port install libtool"
      echo ""
      error="1"
    fi
  fi
  
  if isMissing "make"; then
    echo "make not installed.  Please install with:"
    echo "pacman -S make"
    echo "or"
    echo "apt-get install make"
    echo ""
    error="1"
  fi

  if isMissing "cmake"; then
    echo "make not installed.  Please install with:"
    echo "pacman -S mingw-cmake"
    echo "or"
    echo "apt-get install cmake"
    echo ""
    error="1"
  fi

  if isMissing "autopoint"; then
    echo "autopoint not installed.  Please install with:"
    echo "pacman -S gettext gettext-devel"
    echo "or"
    echo "apt-get install autopoint"
    echo ""
    error="1"
  fi

  if isMissing "msgfmt"; then
    echo "msgfmt not installed.  Please install with:"
    echo "pacman -S gettext gettext-devel"
    echo "or"
    echo "apt-get install gettext"
    echo ""
    error="1"
  fi

  if isMissing "yasm"; then
    echo "yasm not installed.  Please install with:"
    echo "apt-get install yasm"
    echo ""
    error="1"
  fi

  if isMissing "wget"; then
    echo "wget not installed.  Please install with:"
    echo "pacman -S msys/wget"
    echo "or"
    echo "apt-get install wget"
    echo "or"
    echo "sudo port install wget"
    echo ""
    error="1"
  fi


  if [ $(uname -s) == "Darwin" ]; then
    if isMissing "gsed"; then
      echo "gsed not installed. Please install with:"
      echo "brew install gnu-sed"
      echo "or"
      echo "port install gsed"
      echo ""
      error="1"
    else
      sed=gsed
    fi
  else
    if isMissing "sed"; then
      echo "sed not installed.  Please install with:"
      echo "pacman -S msys/sed"
      echo "or"
      echo "apt-get install sed"
      echo ""
      error="1"
    else
      sed=sed
    fi
  fi

  if [ $(uname -s) == "Darwin" ]; then
    if isMissing "gtar"; then
      echo "gnu-tar not installed.  Please install with:"
      echo "brew install gnu-tar"
      echo "or"
      echo "port install gnutar && sudo ln -s /opt/local/bin/gnutar /opt/local/bin/gtar"
      echo ""
      error="1"
    fi
  else
    if isMissing "tar"; then
      echo "tar not installed.  Please install with:"
      echo "mingw-get install tar"
      echo "or"
      echo "apt-get install tar"
      echo ""
      error="1"
    fi
  fi

  if isMissing "xz"; then
    echo "xz is not installed. Please install with:"
    echo "apt-get install xz"
    echo "or"
    echo "brew install xz"
    echo ""
    error="1"
  fi

  if isMissing "git" ; then
    echo "git not installed.  Please install with:"
    echo "pacman -S mingw-git"
    echo "or"
    echo "apt-get install git"
    echo ""
    error="1"
  fi

  if isMissing "hg" ; then
    echo "hg not installed.  Please install with:"
    echo "pacman -S msys/mercurial"
    echo "or"
    echo "apt-get install mercurial"
    echo ""
    error="1"
  fi

  if isMissing "svn" ; then
    echo "svn not installed.  Please install with:"
    echo "pacman -S msys/subversion"
    echo "or"
    echo "apt-get install subversion"
    echo ""
    error="1"
  fi

  if isMissing "gperf" ; then
    echo "gperf not installed.  Please install with:"
    echo "pacman -S msys/gperf"
    echo "or"
    echo "apt-get install gperf"
    echo ""
    error="1"
  fi

  if isMissing "perl" ; then
    echo "perl not installed.  Please install with:"
    echo "pacman -S perl"
    echo "or"
    echo "apt-get install perl"
    echo ""
    error="1"
  fi

  if [ $error == "1" ] ; then
    exit 1
  fi
}

function get_arch {
  host=$1
  echo $host | sed "s/-.*//"
}

function get_os {
  host=$1
  echo $host | sed "s/.*-//"
}

function get_abs_dir {
  local relDir="$1"
  pushDir $relDir
  pwd
  popDir
}

scriptDir=$(get_abs_dir $(dirname $0))

main . expat $1
main . faad2 $1
main . ffmpeg $1
main . fontconfig $1
main . freetype2 $1
main . fribidi $1
main . gpac $1
main . liba52 $1
main . libass $1
main . libjpeg $1
main . libmad $1
main . libogg $1
main . libpng $1
main . libpthread $1
main . librtmp $1
main . libsdl2 $1
main . libtheora $1
main . libvorbis $1
main . libxvidcore $1
main . opencore-amr $1
main . x264 $1
main . zlib $1
