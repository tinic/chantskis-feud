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

### Automatic Releases (on push to main)
Every push to the main branch automatically:
1. Generates a version based on: `YYYYMMDD.commits.hash`
2. Builds the app for all platforms
3. Creates a draft GitHub release

### Manual Release
1. Run `./scripts/publish-release.sh` 
2. The script will auto-generate a version based on git history
3. Choose to create a GitHub release when prompted
4. Wait for GitHub Actions to build all platforms
5. Go to GitHub releases page and publish the draft release

### Version Format
Versions are automatically generated in semver format: `0.YYYYMMDD.commits+hash`
- `0` - Major version (always 0 for auto-generated versions)
- `YYYYMMDD` - Commit date (e.g., 20250703)
- `commits` - Total commit count (e.g., 17)
- `+hash` - Git short hash as build metadata (e.g., +3d2401f)

Example: `0.20250703.17+3d2401f`

## Requirements

- Node.js 18+ and Yarn
- Rust toolchain
- Platform-specific dependencies (see Tauri docs)
- GitHub CLI (`gh`) for creating releases