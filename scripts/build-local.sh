#!/bin/bash

# Local build script for Chantskis Feud
# Builds the app for the current platform only

set -e

echo "🎮 Building Chantskis Feud (Local)"
echo "=================================="

cd feud-app

# Install dependencies if needed
if [ ! -d "node_modules" ]; then
    echo "📦 Installing dependencies..."
    yarn install
fi

# Build the app
echo "🏗️  Building app..."
yarn tauri build

# Show build output location
echo ""
echo "✅ Build complete!"
echo "📁 Build artifacts located at:"

# Platform-specific output paths
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "   feud-app/src-tauri/target/release/bundle/dmg/*.dmg"
    echo "   feud-app/src-tauri/target/release/bundle/macos/*.app"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "   feud-app/src-tauri/target/release/bundle/deb/*.deb"
    echo "   feud-app/src-tauri/target/release/bundle/appimage/*.AppImage"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    echo "   feud-app/src-tauri/target/release/bundle/msi/*.msi"
    echo "   feud-app/src-tauri/target/release/bundle/nsis/*.exe"
fi