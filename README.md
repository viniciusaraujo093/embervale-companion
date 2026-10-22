# Embervale Companion

Experimental companion mod for **Enshrouded**, written in C++17. Current development milestone: **v0.7 / C3.2 Vulkan overlay proof of concept**.

> **Status:** experimental, for local testing only. This is **not** a distributable Shroudtopia overlay yet.

## What works

- Localization and a standalone Win32 UI preview.
- Shroudtopia Release adapter lifecycle integration (v0.6); this adapter does **not** yet load the Vulkan overlay automatically.
- A Vulkan loader layer that intercepts swapchain/acquire/present lifecycle and draws a fixed orange square at the upper-right of the final image when explicitly enabled.
- Locally observed successful game startup, world entry, Alt+Tab, resolution changes (including swapchain recreation), and normal exit during C3.2 tests.

## What's not implemented

- Automatic installation of the Vulkan layer through Shroudtopia.
- Reliable detection of the in-game world map being open.
- Markers/icons positioned and scaled with the world map, or fog-of-war filtering.
- A production-grade synchronization and compatibility guarantee.

## Requirements

- Windows, MSVC, CMake 3.21+, Vulkan SDK (headers and loader).
- Enshrouded for runtime tests.
- Shroudtopia SDK header is bundled under `include/shroudtopia/` for the optional adapter.

## Build the experimental Vulkan layer

```powershell
cmake -S . -B build-layer -DBUILD_VULKAN_LAYER=ON -DBUILD_SHROUDTOPIA_ADAPTER=OFF -DBUILD_TESTING=OFF
cmake --build build-layer --config Release --target EmbervaleCompanionVulkanLayer
```

The layer manifest template is `layers/VkLayer_embervale_companion.json`. To run the experiment, stage the DLL and manifest together in a local test directory and configure the Vulkan loader environment for the Enshrouded process. The developer's local `launch-enshrouded-layer.cmd` is intentionally **not tracked**, because it embeds machine-specific absolute paths.

For the currently tested setup, `EMBERVALE_C2_SQUARE=1` enables the diagnostic square. Keep this experiment disabled for normal play.

## Repository layout

- `src/`: C++ sources, including `vulkan_layer.cpp`.
- `include/`: project and Shroudtopia headers.
- `assets/locales/`: EN and PT-BR translations.
- `layers/`: Vulkan layer manifest.
- `tests/`: localization tests.
- `docs/INTEGRATION.md`: Shroudtopia v0.6 integration checkpoint (historical).
- `docs/EMBERVALE_HANDOFF_v0.7_C3.2.md`: detailed engineering handoff and next steps.

## Roadmap

1. Preserve the C3.2 checkpoint and audit Vulkan synchronization.
2. Detect when the native Enshrouded world map is visible without interfering with rendering.
3. Draw markers aligned with map zoom and pan, ideally respecting unexplored fog of war.
4. Package the overlay for normal Shroudtopia installation without requiring end-user Steam launch options.

The v1.0 priority is **markers on the original world map**, not a minimap or advanced marker styling.

## Safety

Do not distribute third-party game assets. Keep build outputs, logs, local launch scripts, and machine-specific paths out of Git. See `CONTRIBUTING.md`.
