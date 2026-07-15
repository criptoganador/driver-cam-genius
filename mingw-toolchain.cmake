# mingw-toolchain.cmake
# Toolchain para MSYS2 UCRT64 (MinGW-w64 con GCC)
# Referenciado desde CMakePresets.json — VS Code lo carga automáticamente.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# ─── Compiladores ─────────────────────────────────────────────────────────────
set(CMAKE_C_COMPILER   "C:/msys64/ucrt64/bin/gcc.exe"   CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "C:/msys64/ucrt64/bin/g++.exe"   CACHE FILEPATH "" FORCE)
set(CMAKE_RC_COMPILER  "C:/msys64/ucrt64/bin/windres.exe")

# ─── Herramientas de archivado ────────────────────────────────────────────────
set(CMAKE_AR     "C:/msys64/ucrt64/bin/ar.exe")
set(CMAKE_RANLIB "C:/msys64/ucrt64/bin/ranlib.exe")

# NOTA: NO establecer CMAKE_SYSROOT con MinGW.
# MinGW no es un cross-compilador puro; tiene sus propios paths internos
# hardcodeados en el binario de gcc. Establecer --sysroot rompe la búsqueda
# de stdlib.h y otras cabeceras C del sistema.
