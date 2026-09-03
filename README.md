# HoMM3 BattleLogger

Плагин для Heroes III Complete (SoD) под HD-mode. Пишет все события боя во внешние
файлы в двух форматах одновременно: человекочитаемый `txt` и машинный `jsonl`.

## Установка

1. Скопировать `BattleLogger.dll` в `_HD3_Data/Packs/BattleLogger/`
2. Добавить `"BattleLogger"` в `<Packs>` в `_HD3_Data/Settings/sod.ini`
3. (альтернатива) блок в `patcher_x86.ini` рядом с `patcher_x86.dll`:
   ```ini
   [BattleLogger]
   dll="BattleLogger.dll"
   patch="BattleLogger_Init"
   ```

Логи создаются в `Logs/` рядом с `Heroes3 HD.exe`.

## Сборка

Нужны: MSVC 2022 Build Tools (v143), CMake, Ninja, just.

```powershell
just build     # configure + build (x86)
just copy-lc   # копия в папку игры
just fmt       # clang-format
just check     # compile_commands.json для clangd
```

Вручную:
```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

## Что логируется

- `battle_start` / `battle_end` - границы боя
- `stack` - снапшот армий обеих сторон (type, count, позиция)
- `active` - активный стек (luck, morale, hpLost)
- `round` - раунды
- `killed` - гибель существ в стеке
- `rng` - сид ГСЧ на каждый ход (задел под реплей)

## Формат

`Battle_<timestamp>.jsonl` - одна строка = одно событие:
```json
{"tick":4494578,"round":0,"type":"stack","attacker":"side0 slot0","extra":"type=139 count=1 pos=86 luck=0 morale=0"}
{"tick":4494578,"round":0,"type":"active","attacker":"type 139 x1","extra":"side 0 pos 86 luck=0 morale=0 hpLost=0"}
{"tick":4501296,"round":0,"type":"killed","attacker":"side1 slot3 type139","killed":1,"extra":"alive 0"}
```

`Battle_<timestamp>.txt` - то же самое читаемо:
```
=== HoMM3 Battle Log === Battle_2026-09-03_20-44-36_1_poll
[stack] side0 slot0 type=139 count=1 pos=86
-- Round 1 --
[killed] side1 slot3 type139 alive 0
[END] finished
```

## Как работает

Определение боя - опрос указателя `H3CombatManager` (`0x699420`) из рабочего потока
каждые 50мс. LoHook на адресах старта/конца боя не используются: они перекрыты
`HD_SOD.dll` и вызывают access violation при Wait. Определение гибели - сравнение
`numberAlive` стеков между опросами. Загрузка - HD-пак или `patcher_x86.dll`.

## Технические заметки

- Только x86 (`vcvars32`): Heroes3.exe - 32-битный процесс
- `/MT` - статический CRT, без зависимости от vcruntime
- Хедеры H3API подключены как submodule (`extern/H3API`)
- Проверено: SoD Complete 3.2 + HD 5.3 R4 + SoD_SP 1.19.3.15
