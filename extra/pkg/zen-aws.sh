
function aws_build {
  lazy_download "aws.tar.gz" "https://github.com/aws/aws-sdk-cpp/archive/1.4.40.tar.gz"
  lazy_extract "aws.tar.gz"
  mkgit "aws"

  # remove compiler flags currently leaking all the way down to the client app!
  sed '/list(APPEND AWS_COMPILER_FLAGS "-fno-exceptions" "-std=c++${CPP_STANDARD}")/d' \
    -i aws/cmake/compiler_settings.cmake

  mkdir -p aws/bin/$host
  pushDir aws/bin/$host
  cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS=-I$PREFIX/include \
    -DCMAKE_LD_FLAGS=-L$PREFIX/lib \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DENABLE_TESTING=OFF \
    -DBUILD_ONLY="s3;mediastore;mediastore-data" \
    ../..
  $MAKE
  $MAKE install
  popDir
}

function aws_get_deps {
  echo libcurl
  echo zlib
}

