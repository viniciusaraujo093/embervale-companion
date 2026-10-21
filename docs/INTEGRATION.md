# Shroudtopia integration — v0.6 checkpoint

Upstream project: https://github.com/s0t7x/shroudtopia

## Verified locally

- `EmbervaleCompanionShroudtopia.dll` built in **Release** and exported `CreateModInstance`.
- Shroudtopia registered the mod and invoked `Load`, `Activate`, and `Update` successfully.
- Setting the inherited `Mod::loaded` and `Mod::active` flags in the lifecycle callbacks prevented repeated activation attempts at the loader's update interval.
- The game reached its main menu with the Release adapter installed.
- The **Debug** adapter failed to launch the game and logged corrupted mod metadata; do not install Debug builds.

## Installation

See the root README. Install the loader separately, copy only the Release adapter DLL into the loader's `mods` directory, and set `mods["Embervale Companion"].active` to `true` in `shroudtopia.json`.

## Limitations and next steps

- No graphics API hooks, overlay, minimap, map data, or player tracking yet.
- `Deactivate` and `Unload` are implemented but have not been confirmed in an in-game lifecycle test.
- Compatibility with other Shroudtopia releases, Enshrouded updates, or other proxy loaders is not guaranteed.
- Verify third-party header compatibility and license when updating the SDK. The bundled header is under MIT; see `licenses/SHROUDTOPIA-LICENSE.txt`.
- Before building an overlay, validate safe initialization, frame lifecycle, and cleanup paths.
