$ErrorActionPreference = "Stop"

# Ruta al compilador
$compiler = "g++"

# Flags modernos pero ESTABLES
$cxx_flags = @("-std=c++23", "-Wall", "-Wextra")

# Limpieza
if (Test-Path "build") { Remove-Item -Recurse -Force "build" }
New-Item -ItemType Directory -Path "build" | Out-Null

Write-Host "--- Compilando todo el proyecto ---" -ForegroundColor Cyan

# Compilamos y enlazamos todo directamente
# Nota: Como ya no usamos módulos, compilamos los archivos fuente (.cpp) directamente
& $compiler $cxx_flags src/driver_core.cpp src/genius_ilook_317.cpp src/main.cpp -o build/driver_genius.exe -lsetupapi -lwinusb

if (Test-Path "build/driver_genius.exe") {
    Write-Host "¡Build exitoso! Ejecutable listo en build/driver_genius.exe" -ForegroundColor Green
}