# Embervale Companion — v0.1 (esqueleto técnico)

Projeto independente para Windows 11 x64 / Enshrouded Steam.

## Estado real

- Código-fonte de uma DLL mínima e CMake incluídos.
- Ainda **não** há carregamento pelo Enshrouded, interface, atalho, minimapa ou leitura de coordenadas.
- A DLL **não** substitui o `dinput8.dll` do Embervale Atlas.
- Não inclui código ou recursos do Embervale Atlas.

## Compilar no Windows

Instale Visual Studio 2022 com a carga de trabalho "Desenvolvimento para desktop com C++" e CMake.
No terminal de desenvolvedor x64:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

Resultado esperado: `build/Release/EmbervaleCompanion.dll`.

**Não copie a DLL para a pasta do jogo esperando que funcione:** não há carregador integrado.

## Próximo marco

Validar um mecanismo de carregamento permitido/compatível, de preferência sem substituir DLLs existentes. Só então desenvolver interface ImGui e atalho. Fazer backup da instalação e testar offline.
