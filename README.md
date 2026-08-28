# HoMM3 BattleLogger — HD Complete (SoD) + sodSP

Плагин для `Heroes3 HD.exe` (SoD Complete) пишет **все** события боя во внешний файл в 2 формата одновременно.

## План

**v0.1 — Скелет (сейчас):**
- [x] Сборка `BattleLogger.dll` под `MSVC2022 + Ninja` + `clangd` для `zed`
- [x] `BattleLogger` ядро: `Battle_*.jsonl` + `Battle_*.txt` в `HoMM 3 SOD/Logs/`
- [ ] Подключить `H3API` + `patcher_x86` хуки

**v0.2 — Базовый бой (аналитика):**
- Хуки `0x473950` Start / `0x475F90` End / `0x4438D0` Attack / `0x5A3A10` Damage
- Логирование урона, удачи, количества убитых

**v0.3 — Полный лог:**
- Мораль, касты `0x5A3D30`, стрельба, ответки, стены
- `RNG seed` для каждой атаки

**v0.4 — Задел под реплей:**
- Снапшот армий на старте + все броски RNG -> можно восстановить бой

## Toolchain

- **Компилятор:** `MSVC 2022 Build Tools v143` (`/MT` статический CRT). `clangd` только для IDE.
- **Сборка:** `CMake + Ninja` + `compile_commands.json` для `zed`.
- **Загрузка:** через `patcher_x86.dll` (идет с HD). Прописать в `patcher_x86.ini`.

## Сборка (zed / cli)

**Через `just` (рекомендуется):**
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
just --list      # список команд
just build       # configure + build
just copy        # build + копия в HoMM 3 SOD/BattleLogger.dll
just fmt         # clang-format
just check       # проверка compile_commands.json для clangd
```

**Через `cmake` напрямую:**
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --config Release
copy build\BattleLogger.dll "Z:\Games2\HoMM 3 SOD\BattleLogger.dll"
# + добавить блок из patcher_x86.ini.example в patcher_x86.ini
```

В `zed`: `task spawn` -> `configure`/`build`/`copy to HoMM3` или `terminal: just build`. `clangd` подхватит `build/compile_commands.json` автоматом.

**Конфиги:**
- `.clangd:1` - указывает `clangd` на `build/compile_commands.json`, режет MSVC флаги
- `.clang-format:1` - `LLVM 4 spaces, 100 cols`
- `justfile:1` / `.zed/tasks.json:1` - одинаковые таски для cli и zed

## Форматы

`Battle_2026-08-28_12-00-00.jsonl`:
```json
{"tick":12345,"round":1,"type":"attack","attacker":"Pikeman x15","defender":"Griffin x5","damage":42,"killed":2,"luck":1}
{"tick":12346,"round":1,"type":"round","round":2}
```

`Battle_2026-08-28_12-00-00.txt`:
```
=== HoMM3 Battle Log === Battle_2026-08-28_12-00-00
[START] HeroAtk=0 HeroDef=8
-- Round 1 --
[ATTACK] Pikeman x15 -> Griffin x5 | dmg=42 killed=2 LUCK!
```

## Следующий шаг

1. `git clone https://github.com/RoseKavalier/H3API extern/H3API`
2. Раскомментировать `FetchContent_MakeAvailable` в `CMakeLists.txt` или `add_subdirectory`
3. Реализовать `Hooks::Install()` в `src/Hooks.cpp` (заготовки уже есть)
