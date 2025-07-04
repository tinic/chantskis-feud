#!/bin/bash

# Auto-versioning script based on git revision
# Generates version like: 20241204.123.abc1234 (date.commits.hash)

set -e

# Get git information
COMMIT_COUNT=$(git rev-list --count HEAD)
SHORT_SHA=$(git rev-parse --short HEAD)
COMMIT_DATE=$(git show -s --format=%cd --date=format:'%Y%m%d' HEAD)

# Generate version: YYYYMMDD.commits.hash
VERSION="${COMMIT_DATE}.${COMMIT_COUNT}.${SHORT_SHA}"

# Output just the version if --quiet flag is used
if [ "$1" = "--quiet" ]; then
    echo "$VERSION"
    exit 0
fi

echo "🔢 Auto-versioning based on git"
echo "================================"
echo "Commit date: $COMMIT_DATE"
echo "Commit count: $COMMIT_COUNT"
echo "Short SHA: $SHORT_SHA"
echo "Generated version: $VERSION"

# Update package.json
echo ""
echo "📝 Updating feud-app/package.json..."
cd feud-app
npm version "$VERSION" --no-git-tag-version --allow-same-version

# Update tauri.conf.json
echo "📝 Updating feud-app/src-tauri/tauri.conf.json..."
cd src-tauri
# Use Node.js to update the JSON file to preserve formatting
node -e "
const fs = require('fs');
const config = JSON.parse(fs.readFileSync('tauri.conf.json', 'utf8'));
config.version = '$VERSION';
fs.writeFileSync('tauri.conf.json', JSON.stringify(config, null, 2) + '\n');
"

cd ../..

echo "✅ Version updated to: $VERSION"