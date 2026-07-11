# 1. Registrar la infraestructura avanzada de lectura continua en la RAM
if (-not ([System.Management.Automation.PSTypeName]'WinUsbVideoParser').Type) {
    $Signature = @"
    using System;
    using System.Runtime.InteropServices;

    public class WinUsbVideoParser {
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_Initialize(IntPtr DeviceHandle, out IntPtr InterfaceHandle);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_SetCurrentAlternateSetting(IntPtr InterfaceHandle, byte SettingNumber);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_ReadPipe(IntPtr InterfaceHandle, byte PipeId, byte[] Buffer, uint BufferLength, out uint LengthTransferred, IntPtr Overlapped);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_Free(IntPtr InterfaceHandle);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CloseHandle(IntPtr hObject);
    }
"@
    Add-Type -TypeDefinition $Signature
}

$RutaCamara = "\\?\USB#VID_0C45&PID_60B0&MI_00#6&38F4DF72&0&0000#{dee824ef-729b-4a0e-9c14-b7117d33a817}"

Write-Host "`n=== INICIANDO PROCESADOR DE TRAMAS EN TIEMPO REAL ===" -ForegroundColor Cyan
Write-Host "[i] Presiona CTRL+C para detener la captura del bus." -ForegroundColor DarkYellow

$DesiredAccess = [Convert]::ToUInt32("C0000000", 16)
$FlagsAttributes = [Convert]::ToUInt32("40000000", 16)
$hFile = [WinUsbVideoParser]::CreateFile($RutaCamara, $DesiredAccess, 3, [IntPtr]::Zero, 3, $FlagsAttributes, [IntPtr]::Zero)

if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) {
    Write-Host "[-] Error critico de enlace con el hardware." -ForegroundColor Red
    return
}

$WinusbHandle = [IntPtr]::Zero
if ([WinUsbVideoParser]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) {
    
    if ([WinUsbVideoParser]::WinUsb_SetCurrentAlternateSetting($WinusbHandle, 5)) {
        
        $MaxPacketSize = 680
        $BufferPaquete = New-Object byte[] $MaxPacketSize
        $PipeId = 0x81
        $ContadorPaquetes = 0
        $BucleActivo = $true

        # Configurar un timeout corto para no congelar la consola si el bus se vacia
        Write-Host "[+] Escuchando canal isocrono de video de forma continua..." -ForegroundColor Green
        
        try {
            while ($BucleActivo) {
                $BytesLeidos = 0
                
                # Extraccion directa de ráfaga del bus USB
                if ([WinUsbVideoParser]::WinUsb_ReadPipe($WinusbHandle, $PipeId, $BufferPaquete, $MaxPacketSize, [ref]$BytesLeidos, [IntPtr]::Zero)) {
                    if ($BytesLeidos -gt 0) {
                        $ContadorPaquetes++
                        
                        # ALGORITMO DE ANALISIS DE CABECERA SONIX SN9C102:
                        # Las camaras Sonix viejas inyectan secuencias especificas para marcar sincronismo de hardware
                        # Buscamos patrones de inicio de Frame o datos de brillo/exposicion en los primeros bytes
                        $Byte0 = $BufferPaquete[0]
                        $Byte1 = $BufferPaquete[1]
                        
                        if ($ContadorPaquetes % 50 -eq 0) {
                            Write-Host "[Bucle] Paquetes procesados: $ContadorPaquetes | Ultimo bloque: $BytesLeidos bytes | Cabecera: [0x$($Byte0.ToString("X2")), 0x$($Byte1.ToString("X2"))]" -ForegroundColor Gray
                        }
                        
                        # Analizar si es un paquete de sincronismo vertical (Fin/Inicio de imagen)
                        if ($Byte0 -eq 0xFF -and $Byte1 -eq 0xFF) {
                            Write-Host "[!] ¡MARCADOR DE SINCRONISMO DETECTADO EN PAQUETE $ContadorPaquetes! Cambio de Frame detectado en el bus." -ForegroundColor Magnesium
                        }
                    }
                } else {
                    # Si da timeout (121), el bucle sigue intentando pacientemente
                    $ErrorWin32 = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
                    if ($ErrorWin32 -ne 121) {
                        Write-Host "[-] Fallo en el canal de lectura. Codigo Win32: $ErrorWin32" -ForegroundColor Red
                        $BucleActivo = $false
                    }
                }
            }
        } catch {
            Write-Host "`n[-] Captura interrumpida por el usuario." -ForegroundColor Yellow
        }
    } else {
        Write-Host "[-] No se pudo inicializar el Perfil de alto rendimiento." -ForegroundColor Red
    }
    $null = [WinUsbVideoParser]::WinUsb_Free($WinusbHandle)
}

$null = [WinUsbVideoParser]::CloseHandle($hFile)
Write-Host "=== Procesador de tramas cerrado de forma segura ===" -ForegroundColor Cyan