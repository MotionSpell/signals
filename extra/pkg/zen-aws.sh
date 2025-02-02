
function aws_build {
  lazy_download "aws.tar.gz" "https://github.com/aws/aws-sdk-cpp/archive/1.7.251.tar.gz"
  lazy_extract "aws.tar.gz"
  mkgit "aws"

  # remove compiler flags currently leaking all the way down to the client app!
  sed '/list(APPEND AWS_COMPILER_FLAGS "-fno-exceptions" "-std=c++${CPP_STANDARD}")/d' \
    -i aws/cmake/compiler_settings.cmake

  rm -rf aws/bin/$host
  mkdir -p aws/bin/$host
  pushDir aws/bin/$host
  cmake \
    -DOPENSSL_ROOT_DIR=$PREFIX \
    -DCURL_ROOT_DIR=$PREFIX \
    -DCURL_LIBRARY=$PREFIX/lib/libcurl.a \
    -DCURL_INCLUDE_DIR=$PREFIX/include \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS=-I$PREFIX/include \
    -DCMAKE_LD_FLAGS=-L$PREFIX/lib \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DENABLE_TESTING=OFF \
    -DNO_ENCRYPTION=OFF \
    -DBUILD_ONLY="s3;mediastore;mediastore-data" \
    ../..
  $MAKE
  $MAKE install
  popDir
}

function aws_get_deps {
  echo libcurl
  echo openssl
  echo zlib
}

