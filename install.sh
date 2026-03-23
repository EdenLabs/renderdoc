#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Configure if needed or if CMakeCache is stale.
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]] || \
   ! grep -q 'CMAKE_INSTALL_PREFIX:PATH=/usr' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null; then
    echo ":: Configuring..."
    cmake -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DVULKAN_LAYER_FOLDER=/etc/vulkan/implicit_layer.d \
        "${SCRIPT_DIR}"
fi

# Build.
echo ":: Building..."
cmake --build "${BUILD_DIR}"

# Install.
echo ":: Installing (requires sudo)..."
sudo cmake --install "${BUILD_DIR}"

# Fix desktop icon for COSMIC.
# Upstream uses a mimetype-style icon name that COSMIC doesn't resolve for app entries.
echo ":: Fixing desktop icon..."
sudo cp /usr/share/icons/hicolor/scalable/mimetypes/application-x-renderdoc-capture.svg \
        /usr/share/icons/hicolor/scalable/apps/renderdoc.svg
sudo sed -i 's/Icon=application-x-renderdoc-capture/Icon=renderdoc/' \
        /usr/share/applications/renderdoc.desktop
sudo gtk-update-icon-cache -f /usr/share/icons/hicolor/
sudo update-desktop-database /usr/share/applications/

echo ":: Done."
