Write-Host "=== AUTOMATIZANDO INSTALACION DE DEPENDENCIAS DE DESARROLLO (SDL2) ===" -ForegroundColor Cyan

$Url = "https://github.com/libsdl-org/SDL/releases/download/release-2.30.3/SDL2-devel-2.30.3-mingw.zip"
$ZipFile = ".\sdl2_devkit.zip"
$ExtractFolder = ".\sdl2_temp"
$TargetFolder = ".\SDL2"

# Forzar el protocolo TLS 1.2 seguro para evitar fallos de conexión en Windows
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

if (-not (Test-Path $TargetFolder)) {
    Write-Host "[...] Descargando paquete oficial de desarrollo SDL2 desde GitHub..." -ForegroundColor Yellow
    try {
        Invoke-WebRequest -Uri $Url -OutFile $ZipFile -UserAgent "Mozilla/5.0"
        Write-Host "[+] Descarga completada con exito." -ForegroundColor Green
    } catch {
        Write-Host "[-] Error critico al descargar el archivo: $_" -ForegroundColor Red
        Exit 1
    }

    Write-Host "[...] Extrayendo contenedor ZIP en zona temporal..." -ForegroundColor Yellow
    Expand-Archive -Path $ZipFile -DestinationPath $ExtractFolder -Force

    Write-Host "[...] Buscando subcapa de arquitectura x64 (MinGW de 64 bits)..." -ForegroundColor Yellow
    # Localizar la ruta de la carpeta nativa de 64 bits dentro del árbol extraído
    $X64Folder = Get-ChildItem -Path $ExtractFolder -Filter "x86_64-w64-mingw32" -Recurse | Select-Object -First 1

    if ($X64Folder) {
        # Mover los archivos organizados directamente a la carpeta del proyecto
        Move-Item -Path $X64Folder.FullName -Destination $TargetFolder -Force
        Write-Host "[+] Capa de dependencias montada correctamente en: $TargetFolder" -ForegroundColor Green
    } else {
        Write-Host "[-] ERROR: No se localizo la arquitectura x86_64 en el paquete extraido." -ForegroundColor Red
        Exit 1
    }

    # Limpieza absoluta de almacenamiento temporal
    Write-Host "[...] Ejecutando limpieza de residuos temporales..." -ForegroundColor Yellow
    if (Test-Path $ZipFile) { Remove-Item -Path $ZipFile -Force }
    if (Test-Path $ExtractFolder) { Remove-Item -Path $ExtractFolder -Recurse -Force }
    Write-Host "[+] Entorno de dependencias limpio y optimizado." -ForegroundColor Green
} else {
    Write-Host "[i] La carpeta definitiva .\SDL2 ya existe. Saltando inicializacion." -ForegroundColor Blue
}