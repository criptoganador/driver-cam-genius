# Listar todos los dispositivos USB conectados
Write-Host "Buscando dispositivos USB..." -ForegroundColor Cyan
$devices = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "USB*" }

foreach ($dev in $devices) {
    Write-Host "---------------------------------" -ForegroundColor Gray
    Write-Host "Nombre: $($dev.FriendlyName)" -ForegroundColor Yellow
    Write-Host "InstanceID: $($dev.InstanceId)" -ForegroundColor White
}

Write-Host "`nCopia el InstanceID de tu cámara y úsalo para construir el Device Path." -ForegroundColor Green