#!/bin/bash

# Build script for Fast Market Parser

set -e

echo "=== Building Fast Market Data Parser ==="

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring..."
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
echo "Building..."
make -j$(nproc)

echo ""
echo "=== Build Complete ==="
echo ""
echo "Executables:"
echo "  ./build/parser_test       - Run unit tests"
echo "  ./build/parser_demo       - Run interactive demo"
echo "  ./build/parser_benchmark  - Run performance benchmarks"
echo ""
echo "To run tests:"
echo "  cd build && ./parser_test"
echo ""
echo "To run demo:"
echo "  cd build && ./parser_demo"
echo ""
echo "To run benchmark (10M messages):"
echo "  cd build && ./parser_benchmark 10000000"
echo ""
