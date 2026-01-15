#!/bin/bash
set -euo pipefail

chmod 1777 /tmp || true

apt-get update --fix-missing
apt-get install -y cmake g++ python3

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build
cpack -G DEB
cp *.deb /out/
