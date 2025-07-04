#!/bin/bash

# Auto-versioning script based on git revision
# Generates version like: 20241204.123.abc1234 (date.commits.hash)

set -e

# Get git information
COMMIT_COUNT=$(git rev-list --count HEAD)
SHORT_SHA=$(git rev-parse --short HEAD)
COMMIT_DATE=$(git show -s --format=%cd --date=format:'%Y%m%d' HEAD)

# Generate version as semver
# For Windows MSI compatibility, we need to keep numbers under limits:
# Major: 0-255, Minor: 0-255, Patch: 0-65535

# Extract year's last 2 digits, month, and day
YEAR_SHORT=$(echo $COMMIT_DATE | cut -c3-4)
MONTH=$(echo $COMMIT_DATE | cut -c5-6)
DAY=$(echo $COMMIT_DATE | cut -c7-8)

# Create a Windows-compatible version: YY.M.DDCC
# Where YY=year, M=month (1-12), DD=day, CC=commit count
# Remove leading zeros from month
MONTH_NUM=$((10#$MONTH))

# For patch number, we need to ensure it's under 65535
# Format: DCCCC where D is day (1-31) and CCCC is commit count
# This gives us up to 9999 commits per day, total max: 39999 < 65535
PATCH=$((${DAY#0} * 1000 + $COMMIT_COUNT))

# Standard version for all platforms
VERSION="${YEAR_SHORT}.${MONTH_NUM}.${PATCH}"
FULL_VERSION="${VERSION}+${SHORT_SHA}"

# Output just the version if --quiet flag is used
if [ "$1" = "--quiet" ]; then
    echo "$VERSION"
    exit 0
fi

# For full version with hash
if [ "$1" = "--full" ]; then
    echo "$FULL_VERSION"
    exit 0
fi

echo "🔢 Auto-versioning based on git"
echo "================================"
echo "Commit date: $COMMIT_DATE"
echo "Commit count: $COMMIT_COUNT"
echo "Short SHA: $SHORT_SHA"
echo "Generated version: $VERSION"
echo "Full version: $FULL_VERSION"

# Update package.json with semver version
echo ""
echo "📝 Updating feud-app/package.json..."
cd feud-app
npm version "$VERSION" --no-git-tag-version --allow-same-version

# Update tauri.conf.json
echo "📝 Updating feud-app/src-tauri/tauri.conf.json..."
cd src-tauri

# Check if we're building on Windows or if --no-metadata flag is passed
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]] || [[ "$1" == "--no-metadata" ]]; then
    # Windows MSI doesn't support build metadata, use plain semver
    TAURI_VERSION="$VERSION"
else
    # Other platforms can use full version with hash
    TAURI_VERSION="$FULL_VERSION"
fi

# Use Node.js to update the JSON file to preserve formatting
node -e "
const fs = require('fs');
const config = JSON.parse(fs.readFileSync('tauri.conf.json', 'utf8'));
config.version = '$TAURI_VERSION';
fs.writeFileSync('tauri.conf.json', JSON.stringify(config, null, 2) + '\n');
"

# Update Cargo.toml version
echo "📝 Updating feud-app/src-tauri/Cargo.toml..."
# Use sed to update the version line in Cargo.toml
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS sed requires -i ''
    sed -i '' "s/^version = \".*\"/version = \"$VERSION\"/" Cargo.toml
else
    # Linux sed
    sed -i "s/^version = \".*\"/version = \"$VERSION\"/" Cargo.toml
fi

cd ../..

echo "✅ Version updated:"
echo "   package.json: $VERSION (semver compliant)"
echo "   tauri.conf.json: $TAURI_VERSION"
echo "   Cargo.toml: $VERSION (semver compliant)"