#!/bin/sh
set -eu

params=()
for p in "$@" ; do
  case $p in
    -fPIC)
      ;;
    -lpthread)
      ;;
    -pthread)
      ;;
    -Wl,-rpath*)
      ;;
    -g*)
        params+=("$p")
      ;;
    *)
      params+=("$p")
      ;;
  esac
done

params+=("-D_CRT_SECURE_NO_WARNINGS")
params+=("-D_CRT_SECURE_NO_DEPRECATE")
params+=("-D_WINSOCK_DEPRECATED_NO_WARNINGS")
params+=("-DNOMINMAX")
params+=("-Xclang")
params+=("-flto-visibility-public-std")
ccache "/c/Program Files/LLVM/bin/clang++" "${params[@]}"

