# Embervale Companion v0.5 — Vulkan readiness

Independent C++17 project for Enshrouded. **Not yet an in-game mod.**

## New in v0.5

- `EmbervaleCompanionVulkanProbe.exe`: checks that the Windows Vulkan loader is present, reports its API version, creates a Vulkan instance and counts physical devices. **No Vulkan SDK is needed for this probe.** It does not detect which graphics API Enshrouded itself is using.
- `docs/INTEGRATION.md`: proposed Shroudtopia lifecycle adapter and Vulkan UI integration roadmap, clearly separated.
- Win32 preview and localization tests remain available; the PT-BR language label uses a Unicode escape for a source-encoding-independent build.
- Diagnostic DLL is still **not** compatible with any mod loader. Do not copy it into the game folder.

## Windows build

Open in VS Code and run **CMake: Configure** and **CMake: Build** with your MSVC x64 kit. Then from the project directory:

```powershell
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\EmbervaleCompanionVulkanProbe.exe
.\build\Debug\EmbervaleCompanionPreview.exe
```

With a single-configuration generator, executables may instead be in `build\`.

Expected Vulkan probe output includes `Vulkan loader API: ...`, `Vulkan physical devices: 1` (or another nonzero count), and `PASS: Vulkan is available.` This verifies only the local Vulkan runtime, **not** the game's renderer or overlay compatibility.

## Next milestone

Validate the exact loader ABI on the current game version; build a genuine mod-loader lifecycle DLL, then implement Vulkan frame integration and Dear ImGui. No game hooks, injection, or map functionality are claimed in v0.5.

No Embervale Atlas code or assets are included.
