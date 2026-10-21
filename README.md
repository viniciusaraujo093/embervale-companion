# Embervale Companion v0.2 — localization foundation

Independent open-source-ready **source prototype** for Enshrouded (Windows x64). This is **not yet a working in-game mod**.

## What changed

- All code identifiers, comments, and documentation are in English.
- Locale files use flat UTF-8 JSON string dictionaries (`assets/locales/en-US.json` and `pt-BR.json`).
- `Localization` loads the English reference dictionary, switches languages, and falls back to English for missing keys.
- `system_language()` reads the Windows locale. A future UI can choose this by default, then let the user override it.
- Invalid/missing locale files do not replace the active locale.
- A tiny dependency-free parser supports escaped JSON strings and Unicode escape sequences.

## Build (Windows 11, Visual Studio 2022)

Install the **Desktop development with C++** workload. Run in a VS developer terminal:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output: `build/Release/EmbervaleCompanion.dll` and `build/Release/locales/`.

## Using localization in the future UI

```cpp
companion::Localization i18n(localeDirectory);
const auto systemLocale = companion::Localization::system_language();
if (!i18n.set_language(systemLocale)) i18n.set_language("en-US");
// ImGui::TextUnformatted(i18n.get("settings.title").c_str());
// Set up a font with the glyphs required for the selected language.
```

The locale directory should be derived from the DLL installation path by the future bootstrapper, not the process working directory. Persisting language preferences and rendering a selector are future tasks.

## Important limitations

- No loader, UI, map, game hooks, coordinates, or live language selector yet.
- The DLL **must not** be copied into the game directory expecting it to load automatically.
- No code or assets from Embervale Atlas are included.
- The custom parser accepts only flat string-to-string JSON objects, not general JSON.
- The project has not been compiled on Windows or tested inside Enshrouded.

## Next milestone

Choose and validate a compatible loader, then add the ImGui language dropdown and persist settings.
