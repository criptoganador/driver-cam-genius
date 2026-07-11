[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$file = Join-Path $PSScriptRoot 'stream_crudo.raw'
$bytes = [System.IO.File]::ReadAllBytes($file)
$total = $bytes.Length
$kb    = [math]::Round($total / 1024, 1)

Write-Host "=== ANALISIS DEL ARCHIVO RAW ===" -ForegroundColor Cyan
Write-Host "Tamano total : $total bytes ($kb KB)"
Write-Host "Bloques x192 : $([math]::Floor($total/192))"
Write-Host "Bloques x128 : $([math]::Floor($total/128))"

Write-Host ""
Write-Host "=== PRIMEROS 64 BYTES (HEX) ===" -ForegroundColor Yellow
$hex = ($bytes[0..63] | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
Write-Host $hex

Write-Host ""
Write-Host "=== BYTES 192..255 (inicio del 2do bloque de 192) ===" -ForegroundColor Yellow
$hex2 = ($bytes[192..255] | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
Write-Host $hex2

Write-Host ""
Write-Host "=== ESTADISTICAS DE VALORES ===" -ForegroundColor Yellow
$zeros = ($bytes | Where-Object { $_ -eq 0 }).Count
$ff    = ($bytes | Where-Object { $_ -eq 255 }).Count
$pz    = [math]::Round($zeros * 100 / $total, 1)
$pf    = [math]::Round($ff    * 100 / $total, 1)
Write-Host "Bytes 0x00 : $zeros ($pz pct)"
Write-Host "Bytes 0xFF : $ff ($pf pct)"

# Histograma de los 16 valores mas frecuentes
Write-Host ""
Write-Host "=== TOP 8 VALORES MAS FRECUENTES ===" -ForegroundColor Yellow
$freq = @{}
foreach ($b in $bytes) { $freq[$b] = $freq[$b] + 1 }
$top = $freq.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 8
foreach ($e in $top) {
    $pct = [math]::Round($e.Value * 100 / $total, 2)
    Write-Host ("  0x{0:X2} ({1,3}) -> {2,6} veces ({3} pct)" -f $e.Key, $e.Key, $e.Value, $pct)
}

Write-Host ""
Write-Host "=== PRIMER BYTE DE CADA BLOQUE DE 192 (primeros 20) ===" -ForegroundColor Yellow
$headers = @()
for ($i = 0; $i -lt 20 -and ($i * 192) -lt $total; $i++) {
    $headers += '0x{0:X2}' -f $bytes[$i * 192]
}
Write-Host ($headers -join ', ')

Write-Host ""
Write-Host "=== BUSCAR MAGIC BYTES ===" -ForegroundColor Yellow
# JPEG SOI = FF D8
$jpegCount = 0
for ($i = 0; $i -lt ($total - 1); $i++) {
    if ($bytes[$i] -eq 0xFF -and $bytes[$i + 1] -eq 0xD8) {
        Write-Host "  [JPEG SOI] offset $i"
        $jpegCount++
        if ($jpegCount -ge 5) { break }
    }
}
if ($jpegCount -eq 0) { Write-Host "  No se encontraron headers JPEG (FF D8)" }

# Buscar FF 00 (byte stuffing o sync UVC)
$ff00 = 0
for ($i = 0; $i -lt ($total - 1); $i++) {
    if ($bytes[$i] -eq 0xFF -and $bytes[$i + 1] -eq 0x00) { $ff00++ }
}
Write-Host "  Secuencias FF 00 : $ff00"

# Buscar AA 00 (sync Sonix)
$aa00 = 0
for ($i = 0; $i -lt ($total - 1); $i++) {
    if ($bytes[$i] -eq 0xAA -and $bytes[$i + 1] -eq 0x00) { $aa00++ }
}
Write-Host "  Secuencias AA 00 : $aa00"

Write-Host ""
Write-Host "=== ANALISIS DE REPETICION (son los bloques identicos?) ===" -ForegroundColor Yellow
# Compara bloque 0 vs bloque 1
$block0 = $bytes[0..191]
$block1 = $bytes[192..383]
$diffs = 0
for ($i = 0; $i -lt 192; $i++) {
    if ($block0[$i] -ne $block1[$i]) { $diffs++ }
}
Write-Host "Diferencias entre bloque 0 y bloque 1: $diffs bytes de 192"

$block2 = $bytes[384..575]
$diffs2 = 0
for ($i = 0; $i -lt 192; $i++) {
    if ($block0[$i] -ne $block2[$i]) { $diffs2++ }
}
Write-Host "Diferencias entre bloque 0 y bloque 2: $diffs2 bytes de 192"

Write-Host ""
Write-Host "=== RANGO DE VALORES (min/max/promedio) ===" -ForegroundColor Yellow
$min = ($bytes | Measure-Object -Minimum).Minimum
$max = ($bytes | Measure-Object -Maximum).Maximum
$avg = [math]::Round(($bytes | Measure-Object -Average).Average, 2)
Write-Host "Min: $min | Max: $max | Promedio: $avg"

Write-Host ""
Write-Host "=== FIN DEL ANALISIS ===" -ForegroundColor Cyan
