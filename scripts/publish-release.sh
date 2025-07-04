#!/bin/bash

# Chantskis Feud Release Script
# This script builds and packages the Tauri app for release

set -e

echo "🎮 Chantskis Feud Release Builder"
echo "================================"

# Check if we're in the right directory
if [ ! -f "feud-app/package.json" ]; then
    echo "❌ Error: Must run from project root directory"
    exit 1
fi

# Auto-generate version
chmod +x scripts/auto-version.sh
VERSION=$(./scripts/auto-version.sh --quiet)
./scripts/auto-version.sh
echo ""

# Check for uncommitted changes
if ! git diff-index --quiet HEAD --; then
    echo "⚠️  Warning: You have uncommitted changes"
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Build the app
echo "🔨 Building Tauri app..."
cd feud-app

# Clean previous builds
echo "🧹 Cleaning previous builds..."
rm -rf dist
rm -rf src-tauri/target/release/bundle

# Install dependencies
echo "📦 Installing dependencies..."
yarn install

# Build for current platform
echo "🏗️  Building for current platform..."
yarn tauri build

# Find built artifacts
echo "📁 Build artifacts:"
if [ -d "src-tauri/target/release/bundle" ]; then
    find src-tauri/target/release/bundle -type f \( -name "*.dmg" -o -name "*.app" -o -name "*.exe" -o -name "*.msi" -o -name "*.deb" -o -name "*.AppImage" \) -print
fi

cd ..

# Ask if user wants to create a GitHub release
echo ""
read -p "📤 Create GitHub release? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    # Check if gh CLI is installed
    if ! command -v gh &> /dev/null; then
        echo "❌ GitHub CLI (gh) is not installed"
        echo "Install it from: https://cli.github.com/"
        exit 1
    fi

    # Create a tag
    TAG="v$VERSION"
    echo "🏷️  Creating tag: $TAG"
    
    # Check if tag already exists
    if git rev-parse "$TAG" >/dev/null 2>&1; then
        echo "⚠️  Tag $TAG already exists"
        read -p "Delete and recreate? (y/N) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            git tag -d "$TAG"
            git push origin --delete "$TAG" 2>/dev/null || true
        else
            exit 1
        fi
    fi
    
    git tag -a "$TAG" -m "Release $VERSION"
    git push origin "$TAG"
    
    # Wait for GitHub Actions to create the draft release
    echo "⏳ Waiting for GitHub Actions to create draft release..."
    echo "Check: https://github.com/$(git remote get-url origin | sed 's/.*github.com[:/]\(.*\)\.git/\1/')/actions"
    echo ""
    echo "The GitHub Actions workflow will:"
    echo "1. Build for all platforms (Windows, macOS, Linux)"
    echo "2. Create a draft release with all artifacts"
    echo "3. You can then publish the release from GitHub"
fi

echo "✅ Build complete!"