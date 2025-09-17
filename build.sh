#!/bin/sh

# Build script for ExtremeTuxRacer in nix develop shell

set -e

# Ensure we are in the project root
cd "$(dirname "$0")"

# Configure if not already
if [ ! -f Makefile ]; then
    ./configure
fi

# Build
make -j$(nproc)

echo "Build complete. Binary is at src/etr"

# Copy binary to project root before cleanup
cp src/etr build/etr

make distclean

echo "Binary preserved as ./etr"
