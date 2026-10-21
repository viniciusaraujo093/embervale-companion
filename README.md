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

## Third-Party Dependencies

### Shroudtopia

Embervale Companion uses [Shroudtopia](https://github.com/s0t7x/shroudtopia), an experimental mod loader for Enshrouded, to integrate with the game process.

- **Project:** [s0t7x/shroudtopia](https://github.com/s0t7x/shroudtopia)
- **Author:** Miguel Oppermann
- **License:** MIT
- **License notice:** [licenses/SHROUDTOPIA-LICENSE.txt](licenses/SHROUDTOPIA-LICENSE.txt)
- **Bundled SDK header:** `include/shroudtopia/shroudtopia.h`

Shroudtopia is an independent third-party project. Embervale Companion is not affiliated with or officially endorsed by its author.

The Shroudtopia loader must be installed separately. Embervale Companion does not redistribute the loader binaries.

### Building the Shroudtopia Adapter

The Shroudtopia adapter must be built in **Release** configuration.

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The resulting adapter is located at:

`build/Release/EmbervaleCompanionShroudtopia.dll`

Copy this DLL into the `mods` directory of the Enshrouded installation.

**Important:** Debug builds are not supported for in-game use. During integration testing, the Debug adapter caused a startup failure, while the Release adapter successfully completed mod registration, loading, activation, and its first update.

### Enabling the Mod

Add the following entry inside the `mods` object in `shroudtopia.json`:

```json
"Embervale Companion": {
    "active": true
}
```

The current integration is experimental and provides lifecycle diagnostics only. In-game map rendering has not yet been implemented.
