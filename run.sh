#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Configure if needed or if CMakeCache is stale.
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]] || \
   ! grep -q 'ENABLE_WAYLAND_UI:BOOL=ON' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null; then
    echo ":: Configuring..."
    cmake -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DENABLE_WAYLAND_UI=ON \
        -DBUILD_VERSION_STABLE=ON \
        "${SCRIPT_DIR}"
fi

# Build.
echo ":: Building..."
cmake --build "${BUILD_DIR}" -j"$(nproc)"

# Run.
echo ":: Starting RenderDoc..."
exec "${BUILD_DIR}/bin/qrenderdoc" "$@"
