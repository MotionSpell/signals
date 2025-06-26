#!/usr/bin/env bash
set -euo pipefail

function has_clang_format
{
  if ! which clang-format >/dev/null ; then
    # Install instructions for different platforms
    if [[ "$OSTYPE" == "darwin"* ]]; then
      echo "On macOS, install clang-format with: brew install clang-format" >&2
    else
      echo "Please install clang-format" >&2
    fi
    return 1
  fi

  return 0
}

if ! has_clang_format ; then
  echo "clang-format not found, skipping reformatting ..." >&2
  exit 0
fi

# Use more portable find syntax
if [[ "$OSTYPE" == "darwin"* ]]; then
  find . \( -name "*.hpp" -o -name "*.cpp" \) | grep -v vcpkg | xargs -L1 clang-format -i
else
  find -name "*.hpp" -or -name "*.cpp" | grep -v vcpkg | xargs -L1 clang-format -i
fi

wait

