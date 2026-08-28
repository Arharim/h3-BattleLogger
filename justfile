# justfile for HoMM_BattleLogger (MSVC2022 + Ninja + zed)
# Requires: just (cargo install just / scoop install just), cmake, ninja, vcvars64
# Usage: just --list, just build, just copy

set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]

build_dir := "build"
homm3_dir := "Z:/Games2/HoMM 3 SOD"
homm3_lc_dir := "Z:/Games2/HoM&M III by LC"
dll_name := "BattleLogger.dll"

default:
    @just --list

# Show toolchain status
env:
    @echo "build_dir={{build_dir}} homm3_dir={{homm3_dir}}"
    @where cl.exe 2>$null || echo "cl.exe not in PATH - run vcvars64.bat"
    @where cmake 2>$null || echo "cmake not found"
    @where ninja 2>$null || echo "ninja not found"
    @where clangd 2>$null || echo "clangd not found (needed for zed)"
    @where clang-format 2>$null || echo "clang-format not found"

# Configure (Ninja + compile_commands for zed/clangd)
# HoMM3 SoD - 32-bit! Используем vcvars32.bat (x86), иначе DLL не загрузится в Heroes3.exe
configure:
    @if (Test-Path "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat") { cmd /c '"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat" && cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON' } elseif (Test-Path "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat") { cmd /c '"C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat" && cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON' } elseif (Get-Command cl.exe -ErrorAction SilentlyContinue) { cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON } else { echo "MSVC not found -> fallback MinGW build (dev only)"; cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_COMPILER="C:/mingw64/bin/g++.exe" -DCMAKE_C_COMPILER="C:/mingw64/bin/gcc.exe" }

configure-msvc:
    @if (Test-Path "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat") { cmd /c '"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat" && cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON' } else { cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON }

configure-debug:
    @if (Test-Path "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat") { cmd /c '"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat" && cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON' } elseif (Get-Command cl.exe -ErrorAction SilentlyContinue) { cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON } else { cmake -B {{build_dir}} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_COMPILER="C:/mingw64/bin/g++.exe" -DCMAKE_C_COMPILER="C:/mingw64/bin/gcc.exe" }

# Build - тоже через vcvars32 чтобы окружение не терялось
build: configure
    @if (Test-Path "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat") { cmd /c '"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat" && cmake --build {{build_dir}} --config Release' } else { cmake --build {{build_dir}} --config Release }

build-debug: configure-debug
    @if (Test-Path "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat") { cmd /c '"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat" && cmake --build {{build_dir}} --config Debug' } else { cmake --build {{build_dir}} --config Debug }

rebuild:
    @if (Test-Path {{build_dir}}) { Remove-Item -Recurse -Force {{build_dir}} }
    @just build

# Copy DLL to game folder (default: HoMM 3 SOD)
copy: build
    @New-Item -ItemType Directory -Force -Path "{{homm3_dir}}/Logs" | Out-Null
    Copy-Item "{{build_dir}}/{{dll_name}}" "{{homm3_dir}}/{{dll_name}}" -Force
    @if (-not (Test-Path '{{homm3_dir}}/patcher_x86.ini')) { Set-Content -LiteralPath '{{homm3_dir}}/patcher_x86.ini' -Value "[BattleLogger]`ndll=`"BattleLogger.dll`"`npatch=`"BattleLogger_Init`"" -Encoding ASCII; echo "Created {{homm3_dir}}/patcher_x86.ini" } elseif (-not (Select-String -LiteralPath '{{homm3_dir}}/patcher_x86.ini' -Pattern "BattleLogger" -Quiet)) { Add-Content -LiteralPath '{{homm3_dir}}/patcher_x86.ini' -Value "`n[BattleLogger]`ndll=`"BattleLogger.dll`"`npatch=`"BattleLogger_Init`"" -Encoding ASCII; echo "Patched {{homm3_dir}}/patcher_x86.ini" } else { echo "patcher_x86.ini already has BattleLogger" }
    @echo "Copied -> {{homm3_dir}}/{{dll_name}}"

# Copy to HoM&M III by LC (тестовая SoD) - используем cmd чтобы & не ломал PowerShell
copy-lc: build
    @New-Item -ItemType Directory -Force -Path "{{homm3_dir}}/Logs" | Out-Null
    @New-Item -ItemType Directory -Force -Path '{{homm3_lc_dir}}/Logs' | Out-Null
    @New-Item -ItemType Directory -Force -Path '{{homm3_lc_dir}}\_HD3_Data\Packs\BattleLogger' | Out-Null
    @if (-not (Test-Path '{{homm3_lc_dir}}\_HD3_Data\Packs\BattleLogger\Files.ini')) { Set-Content -LiteralPath '{{homm3_lc_dir}}\_HD3_Data\Packs\BattleLogger\Files.ini' -Value "; BattleLogger`r`n" -Encoding ASCII }
    @cmd /c copy /Y "{{build_dir}}\{{dll_name}}" "Z:\Games2\HoM&M III by LC\{{dll_name}}"
    @cmd /c copy /Y "{{build_dir}}\{{dll_name}}" "Z:\Games2\HoM&M III by LC\_HD3_Data\Packs\BattleLogger\{{dll_name}}"
    @if (-not (Test-Path '{{homm3_lc_dir}}/patcher_x86.ini')) { Set-Content -LiteralPath '{{homm3_lc_dir}}/patcher_x86.ini' -Value "[BattleLogger]`ndll=`"BattleLogger.dll`"`npatch=`"BattleLogger_Init`"" -Encoding ASCII; echo "Created {{homm3_lc_dir}}/patcher_x86.ini" } elseif (-not (Select-String -LiteralPath '{{homm3_lc_dir}}/patcher_x86.ini' -Pattern "BattleLogger" -Quiet)) { Add-Content -LiteralPath '{{homm3_lc_dir}}/patcher_x86.ini' -Value "`n[BattleLogger]`ndll=`"BattleLogger.dll`"`npatch=`"BattleLogger_Init`"" -Encoding ASCII; echo "Patched {{homm3_lc_dir}}/patcher_x86.ini" } else { echo "patcher_x86.ini already has BattleLogger" }
    @echo "Copied -> {{homm3_lc_dir}}/{{dll_name}} and Packs/BattleLogger"

# Copy to both installs
copy-all: build
    @New-Item -ItemType Directory -Force -Path "{{homm3_dir}}/Logs" | Out-Null
    @New-Item -ItemType Directory -Force -Path '{{homm3_lc_dir}}/Logs' | Out-Null
    Copy-Item "{{build_dir}}/{{dll_name}}" "{{homm3_dir}}/{{dll_name}}" -Force
    @cmd /c copy /Y "{{build_dir}}\{{dll_name}}" "Z:\Games2\HoM&M III by LC\{{dll_name}}"
    @echo "Copied -> {{homm3_dir}}/{{dll_name}} and {{homm3_lc_dir}}/{{dll_name}}"

install: copy

# Clean
clean:
    @if (Test-Path {{build_dir}}) { Remove-Item -Recurse -Force {{build_dir}}; echo "cleaned {{build_dir}}" } else { echo "nothing to clean" }

# Format
fmt:
    clang-format -i include/*.h src/*.cpp

fmt-check:
    clang-format --dry-run --Werror include/*.h src/*.cpp

# Verify compile_commands.json for clangd
check:
    @if (Test-Path {{build_dir}}/compile_commands.json) { echo "compile_commands.json OK"; Get-Content {{build_dir}}/compile_commands.json | Select-Object -First 5 } else { echo "no {{build_dir}}/compile_commands.json - run just configure" }

# Run game (test DLL load)
run:
    @Start-Process "{{homm3_dir}}/Heroes3 HD.exe"

run-lc:
    @Start-Process "{{homm3_lc_dir}}/Heroes3 HD.exe"

vcvars:
    @echo 'Run: & "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat"  # HoMM3 32-bit'
    @echo 'or:  & "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat"  # x64 (не для HoMM3)'
    @echo 'or:  & "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars32.bat"'
