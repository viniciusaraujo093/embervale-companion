# Embervale Companion v0.3 — localization tests

Independent C++17 prototype for Enshrouded. **Not an installable in-game mod yet.**

## Build and test on Windows 11 (VS Code + Build Tools 2026)

Open the project folder in VS Code, select the x64 MSVC kit, and run **CMake: Configure**, then **CMake: Build**. From the project root in PowerShell:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Expected: `100% tests passed`. The test executable is `build/Debug/EmbervaleCompanionTests.exe` with Visual Studio's multi-configuration generator. You can run it directly:

```powershell
.\build\Debug\EmbervaleCompanionTests.exe .\build\Debug\locales
```

For Release, build the Release configuration and replace `Debug` with `Release` in the commands.

If CMake tests are not available, run **CMake: Delete Cache and Reconfigure**, then build again.

## What is tested

- English default; switching to Brazilian Portuguese and back
- UTF-8 accents and JSON Unicode escape decoding
- Missing-key markers and English fallback for incomplete translations
- Missing or invalid locale files, and rejection of unsafe locale names

## Current limitations

- The DLL compiles but has no game loader, map, hooks, UI, or player coordinates.
- Tests validate localization code, **not** Enshrouded compatibility.
- No Embervale Atlas code or assets are used.
- Translation JSON supports only flat string-to-string objects.

## Next milestone

Research a compatible, documented game loading mechanism and prototype an in-game UI without copying proprietary mod code.
