Write-Host "=== INICIANDO COMPILACION DEL RENDERIZADOR C++ ===" -ForegroundColor Cyan

# Detectar el directorio base absoluto del script de forma dinamica
$BASE_DIR = $PSScriptRoot
if ([string]::IsNullOrEmpty($BASE_DIR)) { 
    $BASE_DIR = (Get-Location).Path 
}

# Construir rutas absolutas robustas
$SDL2_DIR = Join-Path $BASE_DIR "SDL2"
$INCLUDE_PATH = Join-Path $SDL2_DIR "include"
$LIB_PATH = Join-Path $SDL2_DIR "lib"

# Validar fisicamente que la subcarpeta de cabeceras de SDL2 exista antes de llamar a GCC
if (-not (Test-Path (Join-Path $INCLUDE_PATH "SDL2\SDL.h"))) {
    Write-Host "[-] ERROR CRITICO: No se encuentra 'SDL.h' en la ruta absoluta calculada." -ForegroundColor Red
    Exit 1
}

Write-Host "[...] Compilando reproductor.cpp con Runtime de MinGW y optimizacion O3..." -ForegroundColor Yellow

# g++ requiere estrictamente el orden: Fuente -> Rutas -> Libreria Base -> Componentes Graficos -> Sistema
g++ -O3 reproductor.cpp -o reproductor.exe `
    "-I$INCLUDE_PATH" `
    "-L$LIB_PATH" `
    -lmingw32 -lSDL2main -lSDL2 -lkernel32

if ($LastExitCode -eq 0) {
    # Mapear y asegurar la DLL al lado del ejecutable para el enlace dinamico
    $DllSource = Join-Path $LIB_PATH "SDL2.dll"
    $DllDest = Join-Path $BASE_DIR "SDL2.dll"
    
    if (Test-Path $DllSource) {
        Copy-Item -Path $DllSource -Destination $DllDest -Force
    }
    Write-Host "[+] EXITO: 'reproductor.exe' compilado y enlazado correctamente." -ForegroundColor Green
} else {
    Write-Host "[-] ERROR: Fallo la compilacion estructural. Revisa los logs de g++ superiores." -ForegroundColor Red
}