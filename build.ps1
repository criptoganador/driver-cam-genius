$ErrorActionPreference = "Stop"

# --- 1. RUTA DEL COMPILADOR PORTABLE (GCC 16) ---
# Usamos el motor g++.exe de tu nueva carpeta MinGW
$compiler = "C:\mingw64\bin\g++.exe"

if (-Not (Test-Path $compiler)) {
    Write-Host "ERROR CRÍTICO: No se encontró g++ en $compiler." -ForegroundColor Red
    exit
}

# --- 2. LIMPIEZA DE ARQUITECTURA ---
if (Test-Path "build") { Remove-Item -Recurse -Force "build" }
New-Item -ItemType Directory -Path "build" | Out-Null

# --- 3. COMPILACIÓN (Usando C++23 Nativo) ---
Write-Host "--- Compilando Core ---" -ForegroundColor Cyan
# Fíjate que ya NO pasamos rutas manuales (-I). GCC sabe dónde está todo.
& $compiler -std=c++23 -c src/driver_core.cpp -o build/driver_core.o

Write-Host "--- Compilando Main ---" -ForegroundColor Cyan
& $compiler -std=c++23 -c src/main.cpp -o build/main.o

# --- 4. ENLAZADO ---
Write-Host "--- Enlazando ---" -ForegroundColor Cyan
& $compiler build/driver_core.o build/main.o -o build/driver_genius.exe

# --- 5. VERIFICACIÓN FINAL ---
if (Test-Path "build/driver_genius.exe") {
    Write-Host "¡Build exitoso! Archivo creado en build/driver_genius.exe" -ForegroundColor Green
} else {
    Write-Host "Error: El ejecutable no se creó." -ForegroundColor Red
}