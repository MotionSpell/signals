#!/bin/bash

# Check if a prefix argument is provided
if [ -z "$1" ]; then
    echo "Error: No prefix path provided."
    echo "Usage: ./cmake-build-dep.sh <prefix-path>"
    exit 1
fi

# Get the prefix argument
PREFIX=$1

# Resolve PREFIX to an absolute path
ABSOLUTE_PREFIX=$(realpath "$PREFIX")

# Set the SYSROOT_PATH environment variable to the resolved absolute prefix
export SYSROOT_PATH="$ABSOLUTE_PREFIX"

# Print a message to confirm SYSROOT_PATH is set
echo "Setting SYSROOT_PATH to absolute path: $SYSROOT_PATH"

# Run extra.sh with the absolute PREFIX variable to build the dependencies
echo "Running extra.sh to build dependencies with PREFIX=$ABSOLUTE_PREFIX"
PREFIX="$ABSOLUTE_PREFIX" ./extra.sh


# End of the script
echo "Dependencies have been built. SYSROOT_PATH is set to: $SYSROOT_PATH"
