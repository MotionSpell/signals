#!/bin/bash
#
# Download, build and locally deploy external dependencies
#
build_ffmpeg=true
build_gpac=true
set -x
mkdir -p ../sysroot
PREFIX=`realpath ../sysroot`
mkdir -p tmp/build-sysroot/downloads
pushd tmp/build-sysroot
if $build_ffmpeg; then
	wget "http://ffmpeg.org/releases/ffmpeg-4.3.8.tar.bz2" -c -O "downloads/ffmpeg-4.3.8.tar.bz2" --no-verbose
	tar xfv "downloads/ffmpeg.tar.bz2"
	mkdir -p ffmpeg-4.3.8/build
	pushd ffmpeg-4.3.8/build
	../configure \
		--prefix=$PREFIX \
		--enable-pthreads \
		--disable-w32threads \
		--disable-debug \
		--disable-doc \
		--disable-static \
		--enable-shared \
		--enable-libopenh264 \
		--enable-zlib \
		--disable-programs \
		--disable-gnutls \
		--disable-openssl \
		--disable-iconv \
		--disable-bzlib \
		--enable-avresample \
		--disable-decoder=mp3float \
		--pkg-config=pkg-config
	make
	make install
	popd
fi
if $build_gpac; then
	if [ ! -d gpac ]; then
		git clone https://github.com/gpac/gpac.git gpac
	fi
	pushd gpac
	git checkout -q "c02c9a2fb0815bfd28b7c4c46630601ad7fe291d"
	git submodule update --init
	mkdir -p build
	pushd build
	os=Darwin
	../configure \
		--target-os=$os \
		--extra-cflags="-I$PREFIX/include -w -fPIC" \
		--extra-ldflags="-L$PREFIX/lib" \
		--verbose \
		--prefix=$PREFIX \
		--disable-player \
		--disable-ssl \
		--disable-alsa \
		--disable-jack \
		--disable-oss-audio \
		--disable-pulseaudio\
		 --use-png=no \
		--use-jpeg=no \
		--disable-3d \
		--disable-atsc \
		--disable-avi \
		--disable-bifs \
		--disable-bifs-enc \
		--disable-crypt \
		--disable-dvb4linux \
		--disable-dvbx \
		--disable-ipv6 \
		--disable-laser \
		--disable-loader-bt \
		--disable-loader-isoff \
		--disable-loader-xmt \
		--disable-m2ps \
		--disable-mcrypt \
		--disable-od-dump \
		--disable-odf \
		--disable-ogg \
		--disable-opt \
		--disable-platinum \
		--disable-qtvr \
		--disable-saf \
		--disable-scene-dump \
		--disable-scene-encode \
		--disable-scene-stats \
		--disable-scenegraph \
		--disable-seng \
		--disable-sman \
		--disable-streaming \
		--disable-svg \
		--disable-swf \
		--disable-vobsub \
		--disable-vrml \
		--disable-wx \
		--disable-x11 \
		--disable-x11-shm \
		--disable-x11-xv \
		--disable-x3d \
		--enable-export \
		--enable-import \
		--enable-parsers \
		--enable-m2ts \
		--enable-m2ts-mux \
		--enable-ttxt \
		--enable-hevc \
		--enable-isoff \
		--enable-isoff-frag \
		--enable-isoff-hds \
		--enable-isoff-hint \
		--enable-isoff-write \
		--enable-isom-dump
	make lib
	make install
	make install-lib
	popd
	popd

fi