cmake_minimum_required(VERSION 3.20)
project(HoMM_BattleLogger VERSION 0.1.0 LANGUAGES CXX C)

# --- Toolchain: MSVC2022 + Ninja + clangd ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # для zed/clangd

if(NOT MSVC)
    message(WARNING "Рекомендуется MSVC 2022 (v143). Сейчас: ${CMAKE_CXX_COMPILER_ID}. Для clang-cl используй -T ClangCL")
endif()

# H3API как зависимость (скачается при первом configure если нет)
include(FetchContent)
FetchContent_Declare(
    H3API
    GIT_REPOSITORY https://github.com/RoseKavalier/H3API.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
# Раскомментируй чтобы качать автоматом:
# FetchContent_MakeAvailable(H3API)

# H3API - хедеры + patcher_x86.hpp (32-bit, SoD Complete)
if(EXISTS "${CMAKE_SOURCE_DIR}/extern/H3API/CMakeLists.txt")
    # H3API ставит Win32 принудительно, убираем чтобы не конфликтовало с Ninja
    # Но сама игра 32-bit, поэтому собираем x86
    set(H3API_BUILD_AS_DLL OFF CACHE BOOL "" FORCE)
    # Не добавляем H3API как либу, используем только headers (header-only для наших хуков)
endif()

add_library(BattleLogger SHARED
    src/dllmain.cpp
    src/BattleLogger.cpp
    src/Hooks.cpp
)

target_include_directories(BattleLogger PRIVATE
    include
    extern/H3API/include
)

# Флаги под HoMM3 + patcher_x86 + HD-mode (SoD 3.2 Complete)
target_compile_definitions(BattleLogger PRIVATE
    _WINDOWS
    _USRDLL
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(BattleLogger PRIVATE
        /W3 /MP /GS- /Oi
        /EHsc
        /permissive-
        # /MT = статический CRT чтобы не тащить vcruntime в папку игры
        # Для отладки поменяй на /MTd
        $<$<CONFIG:Release>:/MT>
        $<$<CONFIG:Debug>:/MTd /Od /Zi>
        $<$<CONFIG:Release>:/O2 /Oi /Gy>
    )
    target_link_options(BattleLogger PRIVATE
        /DYNAMICBASE:NO
        /SAFESEH:NO
        $<$<CONFIG:Debug>:/DEBUG>
    )
elseif(MINGW OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Fallback для разработки без MSVC (MinGW / LLVM clang GNU-like)
    # Соберет DLL для теста логики, но для релиза под HD нужен MSVC!
    message(STATUS "Fallback toolchain: ${CMAKE_CXX_COMPILER_ID} - MSVC flags disabled")
    target_compile_options(BattleLogger PRIVATE -Wall -Wextra -O2)
endif()

# Копировать DLL в папку игры после сборки (опционально)
# поменяй путь под свой
set(HOMM3_DIR "Z:/Games2/HoMM 3 SOD" CACHE STRING "Path to HoMM3 Complete")
set(HOMM3_LC_DIR "Z:/Games2/HoM&M III by LC" CACHE STRING "Path to HoMM3 LC test install")
# Экранируем & для cmd (HoM&M -> HoM^&M) иначе ninja падает с '"M" не является командой'
add_custom_command(TARGET BattleLogger POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Built: $<TARGET_FILE:BattleLogger>"
    COMMAND ${CMAKE_COMMAND} -E echo "just copy    -> ${HOMM3_DIR}/BattleLogger.dll"
    COMMAND ${CMAKE_COMMAND} -E echo "just copy-lc -> Z:/Games2/HoM^&M III by LC/BattleLogger.dll"
)

# Для zed: генерация compile_commands.json уже включена
