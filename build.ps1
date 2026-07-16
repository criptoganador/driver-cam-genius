$ErrorActionPreference = "Stop"

# Ruta al compilador
$compiler = "g++"

# Flags modernos pero ESTABLES
$cxx_flags = @("-std=c++23", "-Wall", "-Wextra", "-I./SDL2/include")

# Limpieza (solo borramos el exe viejo para no tener errores de bloqueo de archivos)
if (-not (Test-Path "build")) { New-Item -ItemType Directory -Path "build" | Out-Null }
if (Test-Path "build/driver_genius.exe") { Remove-Item -Force "build/driver_genius.exe" -ErrorAction SilentlyContinue }

Write-Host "--- Compilando todo el proyecto ---" -ForegroundColor Cyan

# Compilamos y enlazamos todo directamente
# Nota: Como ya no usamos módulos, compilamos los archivos fuente (.cpp) directamente
& $compiler $cxx_flags src/driver_core.cpp src/genius_ilook_317.cpp src/bandwidth_manager.cpp src/video_capture.cpp src/main.cpp -o build/driver_genius.exe -L./SDL2/lib -lSDL2 -lsetupapi -lwinusb

if (Test-Path "build/driver_genius.exe") {
    Write-Host "¡Build exitoso! Ejecutable listo en build/driver_genius.exe" -ForegroundColor Green
}