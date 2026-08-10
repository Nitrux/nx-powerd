#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
mkdir -p build
cd build
PACKAGE_VERSION="${PACKAGE_VERSION:-0.0.1}"
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DCPACK_PACKAGE_VERSION="${PACKAGE_VERSION}" ..
cmake --build . --parallel "$(nproc)"
cpack -G DEB -C Release
