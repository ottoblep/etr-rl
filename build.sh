#!/bin/sh

# Build script for ExtremeTuxRacer in nix develop shell

set -e

# Ensure we are in the project root
cd "$(dirname "$0")"

# Regenerate autotools files if needed (or if rayNEAT not present in generated Makefiles)
if [ ! -f configure ] || ! grep -q "rayNEAT" src/Makefile.in 2>/dev/null; then
    echo "Running autogen.sh to regenerate autotools files..."
    ./autogen.sh
fi

# Configure if not already
if [ ! -f Makefile ]; then
    # Ensure we compile with C++17 (needed by rayNEAT headers)
    export CXXFLAGS="${CXXFLAGS} -std=gnu++17"
    ./configure --prefix="$(pwd)/build"
fi

# Clean previous build
make clean

# Build
make -j$(nproc)

# Install
make install

echo "Build complete. Binary is at build/bin/etr"

# Copy binary to project root before cleanup
cp build/bin/etr build/etr

make distclean

echo "Binary preserved as ./etr"
