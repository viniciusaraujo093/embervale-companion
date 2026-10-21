# Embervale Companion v0.6 — Mod-loader lifecycle experiment

Independent C++17 project for Enshrouded. **Not yet an in-game map or overlay.**

## New in v0.6

- Optional Shroudtopia lifecycle adapter in `src/shroudtopia_adapter.cpp` implementing `CreateModInstance`, `Load`, `Unload`, `Activate`, `Deactivate`, and `Update`.
- Appends diagnostic events to `embervale-companion.log` beside the host executable, if writable. The first update is logged once to avoid flooding.
- **The adapter is only built when the exact upstream Shroudtopia SDK header is provided.** This avoids inventing an incompatible C++ ABI. Loader compatibility with Enshrouded 1.0 remains unverified.
- All v0.5 diagnostics, localization tests, and Win32 preview remain.

## Build and test on Windows

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

To enable the loader adapter, download the official `shroudtopia.h` from the source version matching your installed Shroudtopia release, then configure:

```powershell
cmake -S . -B build -DSHROUDTOPIA_INCLUDE_DIR="C:/path/to/upstream/header/directory"
cmake --build build --config Debug
```

The optional target outputs `EmbervaleCompanionShroudtopia.dll`. If the SDK ABI or metadata layout has changed, compilation may fail: **do not work around this by fabricating a replacement header**. Consult `docs/INTEGRATION.md`.

**Do not copy the diagnostic `EmbervaleCompanion.dll` into the game directory.** Do not install multiple competing proxy DLL loaders without first reviewing their compatibility and backups. Confirm loader release compatibility before trying the optional adapter. No hook, injection, Vulkan overlay, player position, or map feature is implemented yet.

## AI-Assisted Development

Embervale Companion is developed with assistance from AI tools, including OpenAI's ChatGPT.

AI is used for code generation, architectural exploration, documentation, and testing. The project maintainer defines requirements, reviews and tests implementations, makes technical decisions, and is responsible for the code included in the repository.

AI-generated contributions are not assumed to be correct or production-ready without validation.

## Attribution

Shroudtopia is a separate third-party project. Its code and binaries are not bundled here. No Embervale Atlas code or assets are included.
