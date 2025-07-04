#!/bin/bash

# Auto-versioning script based on git revision
# Generates version like: 20241204.123.abc1234 (date.commits.hash)

set -e

# Get git information
COMMIT_COUNT=$(git rev-list --count HEAD)
SHORT_SHA=$(git rev-parse --short HEAD)
COMMIT_DATE=$(git show -s --format=%cd --date=format:'%Y%m%d' HEAD)

# Generate version as semver: 0.YYYYMMDD.commits
# We use 0 as major version to indicate pre-release
# The commit hash will be added as build metadata
VERSION="0.${COMMIT_DATE}.${COMMIT_COUNT}"
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

# Update tauri.conf.json with full version (including hash)
echo "📝 Updating feud-app/src-tauri/tauri.conf.json..."
cd src-tauri
# Use Node.js to update the JSON file to preserve formatting
node -e "
const fs = require('fs');
const config = JSON.parse(fs.readFileSync('tauri.conf.json', 'utf8'));
config.version = '$FULL_VERSION';
fs.writeFileSync('tauri.conf.json', JSON.stringify(config, null, 2) + '\n');
"

cd ../..

echo "✅ Version updated:"
echo "   package.json: $VERSION (semver compliant)"
echo "   tauri.conf.json: $FULL_VERSION (includes git hash)"