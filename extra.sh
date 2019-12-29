#!/bin/bash
#
# Download, build and locally deploy external dependencies
#

set -e
unset EXTRA # security: 'EXTRA' is a signals-specific var, don't use it in zenbuild
export CFLAGS=-w
unset ENABLE_NVIDIA
unset ENABLE_AWS
unset CORES

#TODO add aws
for i in "$@"
do
	case $i in
	--enable-nvidia*)
		echo "enabling nvidia for FFmpeg..."
		export ENABLE_NVIDIA=1
	;;
	--enable-aws*)
		echo "enabling support for AWS SDK"
		export ENABLE_AWS=1
	;;
	--cores=*)
    CORES="${i#*=}"
    shift # past argument=value
	;;
	--help*)
		echo "Zenbuild build tool:"
		echo -e " \t --help prints this message"
		echo -e " \t --enable-nvidia to enable the use of nvidia supported features \
in FFmpeg, please refer to this page: \
https://trac.ffmpeg.org/wiki/HWAccelIntro"
	;;
	*)
	;;
	esac
done

if [ -z "$MAKE" ]; then
	if [[ -n "$CORES" ]]; then
		echo "Number of cores are user defined, overriding system's default"
	elif [ $(uname -s) == "Darwin" ]; then
		CORES=$(sysctl -n hw.logicalcpu)
	else
		CORES=$(nproc)
	fi

	export MAKE="make -j$CORES"
fi

export PREFIX=${PREFIX-}

if [ -z "$PREFIX" ] ; then
  echo "No installation prefix given, please specify the 'PREFIX' environment variable" >&2
  exit 1
fi

echo "Installation prefix: $PREFIX"

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

pushd extra >/dev/null
./zen-extra.sh $CPREFIX
popd >/dev/null

echo "Done"
