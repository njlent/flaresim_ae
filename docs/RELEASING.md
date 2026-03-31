# Releasing

Release shape used so far for `flaresim_ae`.
This doc is the reference for future GitHub releases.

## Versioning

Current tag/title format:
- `vMAJOR.MINOR.PATCH`

Observed examples:
- `v0.1.01` for the first prerelease
- `v0.2.00` for the first stable release

Conventions:
- keep the leading `v`
- keep minor + patch zero-padded to 2 digits
- make the GitHub release title match the tag exactly
- stable release: `prerelease=false`
- prerelease: `prerelease=true`

## Release Shape

For Windows AE builds, publish:
- one GitHub Release
- one attached `.aex` asset
- release notes grouped by change type

Current Windows asset naming:
- `FlareSimAE-v<version>-win-x64.aex`

Example:
- `FlareSimAE-v0.2.00-win-x64.aex`

Local built plugin path:
- `build-ae/src/ae/Debug/FlareSimAE.aex`

Installed AE plugin path used for host validation:
- `C:\Program Files\Adobe\Adobe After Effects 2025\Support Files\Plug-ins\Effects\FlareSimAE.aex`

## Preflight

Before cutting a release:

```bash
git status --short
git fetch --tags origin
git log --reverse --oneline <previous-tag>..HEAD
```

Expectations:
- worktree clean
- `main` pushed
- commit range reviewed since the previous release tag
- release notes drafted from that exact range

## Build And Verify

Run the same local gates used for `v0.2.00`:

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

cmake -S . -B build-ae -DFLARESIM_AE_ENABLE_AE_PLUGIN=ON
cmake --build build-ae --config Debug
ctest --test-dir build-ae -C Debug --output-on-failure
```

Then verify the built plugin exists and hash it:

```bash
cd build-ae\src\ae\Debug
dir FlareSimAE.aex
certutil -hashfile FlareSimAE.aex SHA256
```

If the plugin is already installed into After Effects, verify the installed copy matches the local build by SHA-256.

## Release Notes Structure

Current release notes pattern:
- short intro line
- `## Added since <previous-tag>`
- `## Improved`
- `## Validation`

Use this shape again unless the release is unusually small.

`Validation` should call out:
- local core build/test pass
- AE plugin build/test pass
- host-side AE validation if performed
- concrete host version/date when known

## GitHub Release

Preferred outcome:
- tag + release created on GitHub
- `target_commitish` set to `main`
- `draft=false`
- `prerelease` set per release type
- `generate_release_notes=false`
- mark stable releases as latest

If `gh` is installed and authenticated, it is fine to use it.

If `gh` is unavailable, use the GitHub Releases API. For `v0.2.00`, release creation was done with a small one-off script in ignored `build-ae/` that:
- read GitHub credentials from the configured git credential helper
- created the release via `POST /repos/njlent/flaresim_ae/releases`
- uploaded the `.aex` through the release `upload_url`

Do not commit release helper scripts. `build-ae/` is already ignored and is a safe place for one-off release helpers.

Important:
- the GitHub API can create the tag remotely as part of release creation
- if you use that path, a separate local `git tag` / `git push origin <tag>` step is not required

## Asset Upload

Upload exactly the built plugin artifact, renamed to the release asset format:

- source:
  - `build-ae/src/ae/Debug/FlareSimAE.aex`
- uploaded asset name:
  - `FlareSimAE-v<version>-win-x64.aex`

For `v0.2.00`, uploaded asset:
- `FlareSimAE-v0.2.00-win-x64.aex`

## Host Validation

When possible, validate the exact built binary in After Effects before release.

Current validation pattern:
- install the built `.aex` into the AE plug-ins folder
- confirm the installed binary matches the local build by SHA-256
- run JSX host checks against the installed plugin

Current host checks used:
- effect discovery/add by match name
- parameter enumeration/readback
- output `View` popup switching
- rendered output differences for:
  - Composite
  - Flare Only
  - Bloom Only
  - Sources
  - Diagnostics

Temporary AE debug JSX files should stay ignored, not committed.

## v0.2.00 Reference

`v0.2.00` was released as:
- first stable release
- tag/title: `v0.2.00`
- previous release baseline: `v0.1.01`
- attached asset: `FlareSimAE-v0.2.00-win-x64.aex`

Release notes covered all changes since `v0.1.01`, grouped into:
- `Added since v0.1.01`
- `Improved`
- `Validation`

Main topics called out:
- grouped lens UI + collapsible sections
- CUDA ghost renderer with CPU fallback
- Camera / Aperture / Flare Settings / Post-processing controls
- `Max Sources`
- low-threshold 32-bpc near-white source detection
- source preview + view-mode fixes
- confirmed in-host AE validation

## Checklist

For the next release:

1. review `git log --reverse --oneline <previous-tag>..HEAD`
2. run core + AE build/test gates
3. hash-check `build-ae/src/ae/Debug/FlareSimAE.aex`
4. validate in After Effects if possible
5. draft notes in `Added / Improved / Validation` format
6. create GitHub release with tag/title `vMAJOR.MINOR.PATCH`
7. upload `FlareSimAE-v<version>-win-x64.aex`
8. confirm the release page and asset URL work
