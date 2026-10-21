# Embervale Companion v0.6 — Shroudtopia lifecycle integration

Independent C++17 project for Enshrouded. **The mod loads inside the game, but no in-game map or overlay is implemented yet.**

## Current status

- Win32 standalone preview, UTF-8 localization (`en-US`, `pt-BR`), and CTest localization tests.
- Standalone Vulkan availability probe (does not establish renderer hooking).
- Shroudtopia adapter exports `CreateModInstance` and implements `Load`, `Unload`, `Activate`, `Deactivate`, and `Update`.
- **Locally validated on Windows with Enshrouded and Shroudtopia:** registration, `Load`, a single `Activate`, and the first `Update` were observed in logs; the game started normally using a **Release** build.
- `Deactivate` and `Unload` have not yet been verified during an actual loader lifecycle.
- No Vulkan frame integration, game map, player coordinates, or in-game UI exists yet.

## Build and test (Windows, MSVC)

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The Shroudtopia adapter is enabled by default on Windows when using the bundled upstream SDK header at `include/shroudtopia/shroudtopia.h`. You may specify a different compatible SDK header directory with `-DSHROUDTOPIA_INCLUDE_DIR="C:/path/to/header"`.

To build only the standalone components, without the Shroudtopia SDK or adapter:

```powershell
cmake -S . -B build-standalone -DBUILD_SHROUDTOPIA_ADAPTER=OFF
cmake --build build-standalone --config Debug
ctest --test-dir build-standalone -C Debug --output-on-failure
```

### CMake cache troubleshooting

CMake caches `SHROUDTOPIA_INCLUDE_DIR` in the build directory. If the SDK header was moved or the directory was configured incorrectly in an earlier attempt, changing the default in `CMakeLists.txt` will not overwrite the cached value.

If CMake reports `Shroudtopia header not found`, first confirm that `include/shroudtopia/shroudtopia.h` exists. Then either configure a fresh build directory:

```powershell
cmake -S . -B build-clean
cmake --build build-clean --config Release
ctest --test-dir build-clean -C Release --output-on-failure
```

Or explicitly set the SDK header directory in the existing build:

```powershell
cmake -S . -B build -DSHROUDTOPIA_INCLUDE_DIR="${PWD}/include/shroudtopia"
```

You can also clear the cached setting using `cmake -U SHROUDTOPIA_INCLUDE_DIR -S . -B build` and reconfigure. A fresh build directory is the simplest way to verify that the default configuration works.

### In-game installation (experimental)

**Build the Shroudtopia adapter in Release for installation.** A Debug adapter caused a game startup failure and corrupted metadata during local testing, whereas Release successfully loaded and activated. Do not install the Debug adapter.

1. Install a compatible [Shroudtopia](https://github.com/s0t7x/shroudtopia) loader separately, following its instructions. Check for conflicts with other proxy DLL loaders.
2. Copy `build/Release/EmbervaleCompanionShroudtopia.dll` to `<Enshrouded installation>/mods/` with the game closed.
3. In `<Enshrouded installation>/shroudtopia.json`, add the following inside the existing `mods` object (preserve other entries):

```json
"Embervale Companion": {
  "active": true
}
```

4. Start the game and inspect `shroudtopia.log` and `embervale-companion.log` in the game installation directory.

Only the adapter DLL belongs in `mods/`; **do not copy** the standalone diagnostic `EmbervaleCompanion.dll` there. If startup fails, remove the adapter DLL and retry. The integration has only been verified in the maintainer's local environment; compatibility is not guaranteed across game or loader versions.

## Third-party dependencies

[Shroudtopia](https://github.com/s0t7x/shroudtopia) is an independent third-party mod loader, licensed under MIT. Its upstream `shroudtopia.h` header is included under `include/shroudtopia/` for building the adapter. The original license and copyright notice are retained in [`licenses/SHROUDTOPIA-LICENSE.txt`](licenses/SHROUDTOPIA-LICENSE.txt). Shroudtopia binaries are **not** bundled; install the loader separately. Embervale Companion is not affiliated with or endorsed by the Shroudtopia author.

No Embervale Atlas code or assets are included.

## AI-Assisted Development

Embervale Companion is developed with assistance from AI tools, including OpenAI's ChatGPT. AI is used for code generation, architectural exploration, documentation, and testing. The project maintainer defines requirements, reviews and tests implementations, makes technical decisions, and is responsible for the code included in the repository. AI-generated contributions are not assumed to be correct or production-ready without validation.
