cmake_minimum_required(VERSION 3.20)

# ─── Toolchain: force MSYS2 MinGW g++ BEFORE project() ───────────────────────
# This prevents CMake from picking up the standalone LLVM clang in PATH,
# which lacks the Windows SDK .lib files needed by lld-link.
if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER   "C:/msys64/ucrt64/bin/gcc.exe"   CACHE PATH "" FORCE)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "C:/msys64/ucrt64/bin/g++.exe"   CACHE PATH "" FORCE)
endif()

project(driver_genius
    VERSION     1.0.0
    DESCRIPTION "Driver para camara Genius iLook 317 via WinUSB"
    LANGUAGES   C CXX
)

# ─── C++ standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ─── Build type defaults ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ─── Exportar base de datos de compilación para clangd ────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/driver_core.cpp
    src/genius_ilook_317.cpp
)

add_executable(driver_genius ${SOURCES})

# ─── Include directories ───────────────────────────────────────────────────────
target_include_directories(driver_genius PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    C:/msys64/ucrt64/include
)

# ─── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(driver_genius PRIVATE
    -Wall
    -Wextra
    -O0
)

# ─── Windows libraries ─────────────────────────────────────────────────────────
# MinGW on MSYS2 links against import libs (.a) bundled in ucrt64 —
# no MSVC .lib files required.
target_link_libraries(driver_genius PRIVATE
    setupapi
    winusb
)
