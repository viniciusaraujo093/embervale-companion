# Embervale Companion — engineering handoff (v0.7 C3.2)

## Objective

Create a Shroudtopia-installable Enshrouded companion that overlays POI markers directly on the game's **native world map**. v1.0: map markers first. Minimap, coordinates, marker customization are later. Prefer not to reveal markers under unexplored fog of war, if technically possible.

## Local environment (developer-specific, not hardcoded into tracked scripts)

- Repository: `C:\dev\projects\self\EmbervaleCompanion`
- Game: `D:\SteamLibrary\steamapps\common\Enshrouded`
- Windows / MSVC Visual Studio 18 2026 / CMake / C++17.
- Vulkan SDK used locally: `C:\VulkanSDK\1.4.363.0`.
- Build directory: `build-layer`; target: `EmbervaleCompanionVulkanLayer`.
- Layer log: `build-layer/layer-test/embervale-vulkan-layer.log`.
- VS Code Vulkan header squiggles were solved by adding `C:/VulkanSDK/1.4.363.0/Include` to C/C++ IntelliSense includePath. Terminal CMake configuration already worked.

## Milestones

- v0.2: EN/PT-BR localization.
- v0.3: CTest localization tests.
- v0.4: Win32 preview UI.
- v0.5: standalone Vulkan probe.
- v0.6: Shroudtopia adapter works in Release, with Load/Activate/Update; naive MinHook `vkQueuePresentKHR` interception did not provide working overlay.
- v0.7 A/B1/B2/C1: Vulkan loader layer, swapchain and queue observation, command resources, acquisition logging.
- v0.7 C2: first visible GPU overlay, opaque orange/gold square in top-right via render pass and `vkCmdClearAttachments`, with present semaphore chaining.
- v0.7 C3.1: guard for failed `vkQueueSubmit`, disable overlay and do not forward potentially unsafe present on failure; locally tested successfully.
- v0.7 C3.2: added acquire-based synchronization guard and cleanup logging; user tested all five steps including game world, map, Alt+Tab, resolution changes and normal exit. User reported no crashes; overlay remained visible.

## Last C3.2 test evidence

- Swapchain recreation observed at `1820x1024` (#7) and `2560x1440` (#8), format 37, 3 images.
- `C3.2 swapchain cleanup: vkDeviceWaitIdle result=0` and `B1 swapchain metadata released` observed.
- Present calls continued to 6000, with `result=0` in sampled logs.
- Changing resolution changed the apparent size of the square; not yet investigated. A screenshot showed a large orange square.
- These are **functional smoke tests**, not Vulkan synchronization validation or proof of production safety.

## Build / deploy for local experiment

```powershell
cmake -S . -B build-layer -DBUILD_VULKAN_LAYER=ON -DBUILD_SHROUDTOPIA_ADAPTER=OFF -DBUILD_TESTING=OFF
cmake --build build-layer --config Release --target EmbervaleCompanionVulkanLayer
Copy-Item '.\build-layer\Release\EmbervaleCompanionVulkanLayer.dll' '.\build-layer\layer-test\EmbervaleCompanionVulkanLayer.dll' -Force
```

Test launcher previously used `VK_LAYER_PATH` pointing to `build-layer/layer-test`, `VK_INSTANCE_LAYERS=VK_LAYER_EMBERVALE_companion`, `VK_LOADER_DEBUG=error,warn`, `EMBERVALE_C2_SQUARE=1`, then `%*`. It is ignored by Git because it has absolute machine paths. Manifest is tracked at `layers/VkLayer_embervale_companion.json`; copy/stage it alongside the DLL for testing.

## Architecture

- Main source: `src/vulkan_layer.cpp`.
- Layer intercepts instance/device creation, device queue lookup, swapchain creation/destruction, swapchain image enumeration, acquisition, queue presentation, and cleanup.
- Swapchain observation owns per-image command buffers, image views, framebuffers, binary semaphores and fences, plus render pass.
- On presentation, the layer records the clear-rectangle command, submits it waiting on game present semaphores, signals its own semaphore, then forwards present waiting on that semaphore.
- A global mutex protects bookkeeping; GPU waits and cleanup may block. Failure modes and synchronization need further audit.
- The overlay currently draws in screen space, **not** in map coordinates.

## Important open risks

- Binary semaphore reuse and presentation completion must be validated carefully; fence completion alone does not prove present completion.
- Swapchain recreation, device lost paths, queue-family compatibility, and synchronization under concurrency need deeper testing.
- Vulkan validation layer testing is not yet reported.
- `vkDeviceWaitIdle` during rare swapchain destruction is currently used for cleanup; do not add per-frame waits.
- No end-user Shroudtopia packaging for the Vulkan loader layer exists yet. Do not claim drag-and-drop works.
- The CMake project still declares `VERSION 0.6.0` while the experimental milestone is v0.7; update intentionally when formalizing release/version policy.

## Git repository hygiene checkpoint

An uploaded project ZIP contained `build/`, `build-clean/`, `build-hook/`, `build-layer/`, `.git/`, local `.vscode/`, compiled DLL, and three README variants. The original `.gitignore` ignored only `/build/`; revised to ignore `/build*/` and machine-local launcher. Existing Git index in the ZIP showed only the earlier project source tracked (not generated build folders). Preserve `.git/` locally; never delete it.

- Keep `README.md` as the sole main README, with current status.
- Retain `CONTRIBUTING.md` and `docs/INTEGRATION.md` (historical v0.6 evidence).
- Remove obsolete root `README.txt` and `README-B1.txt` from the working tree if untracked; they are obsolete experimental handouts.
- Never commit DLLs, build output, log files, or hardcoded local launcher paths.

## Next task: C4

First checkpoint Git and push after reviewing `git status`, `git diff --cached`, and ignore rules. Then investigate a reliable signal that the native world map is open; start with passive observation, avoid drawing changes. Only after detection, work on icon placement with zoom/pan and fog-of-war constraints. Keep each experiment small and testable.

## Suggested new-chat starting prompt

> Continue Embervale Companion from v0.7 C3.2. Read `docs/EMBERVALE_HANDOFF_v0.7_C3.2.md` and inspect `src/vulkan_layer.cpp`. C3.2 successfully rendered the orange square through resolution changes and swapchain recreation, but it is experimental. The next task is C4: passively investigate detecting when the native Enshrouded world map is open. Preserve the working checkpoint, avoid unrelated refactors, and provide exact PowerShell test steps. Prioritize markers on the native map for v1.0; minimap is not a priority.
