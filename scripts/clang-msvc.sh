#!/bin/sh
set -eu

params=()
for p in "$@" ; do
  case $p in
    -fPIC)
      ;;
    -lpthread)
      ;;
    -Wl,-rpath*)
      ;;
    -g*)
        params+=("$p")
        params+=("-gcodeview")
      ;;
    *)
      params+=("$p")
      ;;
  esac
done

params+=("-D_CRT_SECURE_NO_WARNINGS")
params+=("-D_CRT_SECURE_NO_DEPRECATE")
params+=("-DNOMINMAX")
params+=("-Xclang")
params+=("-flto-visibility-public-std")
ccache "/c/Program Files/LLVM/bin/clang++" "${params[@]}"

