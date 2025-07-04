# Build and Release Scripts

This directory contains scripts for building and releasing the Chantskis Feud app.

## Scripts

### `build-local.sh`
Builds the Tauri app for your current platform only. This is useful for testing local builds.

```bash
./scripts/build-local.sh
```

### `publish-release.sh`
Builds the app and optionally creates a GitHub release. This script will:
1. Build the app for your current platform
2. Optionally create a git tag and push it to trigger the GitHub Actions release workflow
3. The GitHub Actions workflow will then build for all platforms and create a draft release

```bash
./scripts/publish-release.sh
```

## GitHub Actions Workflows

### `.github/workflows/ci.yml`
Runs on every push and pull request to:
- Test the build on Windows, macOS, and Linux
- Run frontend checks (TypeScript, build)
- Run Rust checks (cargo check, test, clippy)

### `.github/workflows/release.yml`
Triggered when you push a version tag (e.g., `v0.1.0`):
- Builds the app for all platforms:
  - macOS (Intel and Apple Silicon)
  - Windows
  - Linux
- Creates a draft GitHub release with all artifacts
- You can then manually publish the release on GitHub

## Release Process

1. Update version in `feud-app/package.json` and `feud-app/src-tauri/tauri.conf.json`
2. Commit your changes
3. Run `./scripts/publish-release.sh`
4. Choose to create a GitHub release when prompted
5. Wait for GitHub Actions to build all platforms
6. Go to GitHub releases page and publish the draft release

## Requirements

- Node.js 18+ and Yarn
- Rust toolchain
- Platform-specific dependencies (see Tauri docs)
- GitHub CLI (`gh`) for creating releases