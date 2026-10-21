# Embervale Companion v0.4 — standalone UI preview

Independent C++17 project for Enshrouded. **Not yet an in-game mod.**

## What is new

`EmbervaleCompanionPreview.exe` is a native Windows preview window. It demonstrates
English/Brazilian Portuguese language switching, UI controls for coordinates and
minimap, and a search text field. The checkboxes and search are **visual prototypes**:
no map, coordinates, search data, settings persistence, Vulkan rendering, or game
integration is implemented. It does **not** install or inject anything into Enshrouded.

The existing diagnostic `EmbervaleCompanion.dll` and localization unit tests remain.

## Build on Windows

1. Extract the ZIP into a new directory and open it in VS Code.
2. Select the x64 MSVC kit, then run **CMake: Configure** and **CMake: Build**.
3. Run `ctest --test-dir build -C Debug --output-on-failure`.
4. Launch `build\Debug\EmbervaleCompanionPreview.exe` (Visual Studio generator),
   or find the executable under your selected CMake build directory.
5. Change the language dropdown and confirm accented PT-BR text displays correctly.

If the build uses a single-config generator, look under `build\` instead of `build\Debug\`.

## Architecture and next step

The preview uses Win32 controls rather than Dear ImGui or Vulkan. This deliberately
validates the language/UI behavior **without pretending an in-game Vulkan hook works**.
Next, independently verify the documented Enshrouded mod loader interface and Vulkan
render integration before writing a game-facing adapter. Do not install the DLL in the
game folder or overwrite `dinput8.dll` based on this prototype.

No Embervale Atlas code or assets are included.
