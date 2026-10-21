# In-game integration design (not implemented yet)

## Proposed loading strategy

Investigate the open-source Shroudtopia loader (MIT):
https://github.com/s0t7x/shroudtopia

The published interface describes a `Mod` implementation and `CreateModInstance` export, with lifecycle methods `Load`, `Unload`, `Activate`, `Deactivate`, `Update`. **Our current DLL does not implement that ABI** and must not be placed in `mods` yet.

Before writing an adapter, validate the **exact header and binary version** on the user's game version, then build against that header, not a guessed struct layout. Shroudtopia does not automatically provide a Vulkan draw callback. The graphics integration is a separate workstream.

## Graphics workstream

1. Confirm client loader compatibility and nonconflicting installation (existing proxy DLLs may collide).
2. Establish a safe lifecycle and log to a file, with no render hooks.
3. Verify a Vulkan frame hook/render callback with correct synchronization, swapchain lifecycle, and resize handling.
4. Render a minimal Dear ImGui panel with fonts supporting PT-BR UTF-8.
5. Add map content, position acquisition, and persistence only after stable rendering.

Do not copy proprietary code or assets from other mods. Avoid changing game saves. Test on a backed-up installation and keep all experiments reversible.
