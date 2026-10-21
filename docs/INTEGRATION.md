# Shroudtopia integration checkpoint (v0.6)

Upstream: https://github.com/s0t7x/shroudtopia

The upstream README describes a `Mod` C++ class with metadata, `Load`, `Unload`, `Activate`, `Deactivate`, `Update`, and an exported `CreateModInstance` factory. The adapter implements these methods but is deliberately compiled **only** with the upstream header from a compatible release. The README alone is not a stable ABI specification.

## Validation sequence

1. Verify which Shroudtopia release, if any, supports your Enshrouded client version. This is not established by a Vulkan availability probe.
2. Obtain `shroudtopia.h` from that **same** source revision; configure `SHROUDTOPIA_INCLUDE_DIR`.
3. Compile the optional adapter and inspect exports using `dumpbin /exports` (expect `CreateModInstance`).
4. Before installation, back up the game's existing proxy DLLs and check for conflicts with other mod loaders.
5. Follow the loader's official installation guide. Install only the optional `EmbervaleCompanionShroudtopia.dll` in the loader's documented mods location. Never rename it to `winmm.dll`, `dinput8.dll`, or replace a game DLL.
6. Launch the game; inspect Shroudtopia's logs and `embervale-companion.log` beside the host executable. A log file alone does not prove the correct game process loaded the mod; verify the process and lifecycle events.
7. If the game crashes or the mod is not recognized, remove the optional mod and restore backups before further debugging.

## Known limitations

- The adapter does not render UI or hook Vulkan.
- The SDK may have changed since the public README. `ModMetaData` aggregate initialization and the `Mod` interface must be verified against the actual header.
- No support guarantee for the current game version or coexistence with Embervale Atlas.
- No code or binary from Shroudtopia is included.
