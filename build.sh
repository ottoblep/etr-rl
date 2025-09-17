#!/bin/sh

# Build script for ExtremeTuxRacer in nix develop shell

set -e

# Ensure we are in the project root
cd "$(dirname "$0")"

# Configure if not already
if [ ! -f Makefile ]; then
    ./configure --prefix="$(pwd)/build"
fi

# Build
make -j$(nproc)

# Install
make install

echo "Build complete. Binary is at build/bin/etr"

# Copy binary to project root before cleanup
cp build/bin/etr build/etr

make distclean

echo "Binary preserved as ./etr"
