#!/bin/bash
#
# Download, build and locally deploy external dependencies
#

set -e
EXTRA_DIR=$PWD/extra
export CFLAGS=-w
export PKG_CONFIG_PATH=$EXTRA_DIR/lib/pkgconfig

if [ -z "$MAKE" ]; then
	if [ $(uname -s) == "Darwin" ]; then
		CORES=$(sysctl -n hw.logicalcpu)
	else
		CORES=$(nproc)
	fi

	export MAKE="make -j$CORES"
fi

if [ -z "$CPREFIX" ]; then
	case $OSTYPE in
	msys)
		CPREFIX=x86_64-w64-mingw32
		echo "MSYS detected ($OSTYPE): forcing use of prefix \"$CPREFIX\""
		;;
	darwin*|linux-musl)
		CPREFIX=-
		if [ ! -e extra/config.guess ] ; then
			wget -O extra/config.guess 'http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.guess;hb=HEAD'
			chmod +x extra/config.guess
		fi
		HOST=$(extra/config.guess)
		echo "$OSTYPE detected: forcing use of prefix \"$CPREFIX\" (host=$HOST)"
		;;
	*)
		CPREFIX=$(gcc -dumpmachine)
		echo "Platform $OSTYPE detected."
		;;
	esac
fi
if [ -z "$HOST" ]; then
	HOST=$($CPREFIX-gcc -dumpmachine)
fi
echo "Using compiler host ($HOST) with prefix ($CPREFIX)"

case $OSTYPE in
darwin*)
    if [ -z "$NASM" ]; then
        echo "You must set the NASM env variable."
    fi
    ;;
esac


#-------------------------------------------------------------------------------
echo zenbuild extra script
#-------------------------------------------------------------------------------
pushd extra >/dev/null
./zen-extra.sh $CPREFIX
popd >/dev/null

## move files
rsync -ar extra/release/$HOST/* extra/

if [ "$HOST" == "x86_64-linux-gnu" ]; then
	#-------------------------------------------------------------------------------
	echo AWS-SDK
	#-------------------------------------------------------------------------------
	if [ ! -f extra/src/aws/CMakeLists.txt ] ; then
		mkdir -p extra/src
		rm -rf extra/src/aws
		git clone https://github.com/aws/aws-sdk-cpp.git extra/src/aws
		pushd extra/src/aws
		git checkout 1.3.48
		popd
	fi

	if [ ! -f extra/release/aws/releaseOk ] ; then
		rm -rf extra/release/aws
		mkdir -p extra/release/aws
		pushd extra/release/aws
		cmake \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_SOURCE_DIR=../../src/aws \
			-DCMAKE_CXX_FLAGS=-I$EXTRA_DIR/include \
			-DCMAKE_LD_FLAGS=-L$EXTRA_DIR/lib \
			-DCMAKE_INSTALL_PREFIX=$EXTRA_DIR \
			-DENABLE_TESTING=OFF \
			-DBUILD_ONLY="s3;mediastore;mediastore-data" \
			../../src/aws
		$MAKE
		$MAKE install
		popd
		touch extra/release/aws/releaseOk
	fi

fi #"$HOST" == "x86_64-linux-gnu"

echo "Done"
