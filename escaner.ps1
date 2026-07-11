# =======================================================================
# ESCÁNER DE MATRIZ DE HARDWARE NATIVO (SONIX SN9C102 & PIXART PAS106)
# =======================================================================
if (-not ([System.Management.Automation.PSTypeName]'WinUsbProber').Type) {
    $Signature = @"
    using System;
    using System.Runtime.InteropServices;

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct WINUSB_SETUP_PACKET {
        public byte RequestType;
        public byte Request;
        public ushort Value;
        public ushort Index;
        public ushort Length;
    }

    public class WinUsbProber {
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);
        
        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_Initialize(IntPtr DeviceHandle, out IntPtr InterfaceHandle);
        
        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_ControlTransfer(IntPtr InterfaceHandle, WINUSB_SETUP_PACKET SetupPacket, byte[] Buffer, uint BufferLength, out uint LengthTransferred, IntPtr Overlapped);
        
        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_Free(IntPtr InterfaceHandle);
        
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CloseHandle(IntPtr hObject);
    }
"@
    Add-Type -TypeDefinition $Signature
}

Write-Host '=== INICIANDO MOTOR DE INGENIERÍA INVERSA EN BUS SIF ===' -ForegroundColor Cyan

# 1. Localización del dispositivo
$PnpDevice = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0C45&PID_60B0&MI_00*' } | Select-Object -First 1
if (-not $PnpDevice) { Write-Host '[-] Cámara no encontrada en el Administrador de Dispositivos.' -ForegroundColor Red; return }
$CameraPath = '\\?\' + $PnpDevice.InstanceId.Replace('\', '#') + '#{dee824ef-729b-4a0e-9c14-b7117d33a817}'

# 2. Primitivas de bajo nivel corregidas con $null
function Write-Reg ([IntPtr]$Intf, [uint16]$Reg, [byte]$Val) {
    $Pkt = New-Object WINUSB_SETUP_PACKET
    $Pkt.RequestType = 0x41; $Pkt.Request = 0x08; $Pkt.Value = $Reg; $Pkt.Length = 1
    $Buf = New-Object byte[] 1; $Buf[0] = $Val; [uint32]$Transferred = 0
    $null = [WinUsbProber]::WinUsb_ControlTransfer($Intf, $Pkt, $Buf, 1, [ref]$Transferred, [IntPtr]::Zero)
}

function Test-Probe ([IntPtr]$Intf, [byte]$SlaveID, [byte]$GpioState) {
    # Inicializar el modo SIF maestro lento para evitar ruido en placa
    Write-Reg -Intf $Intf -Reg 0x08 -Value 0x14
    Write-Reg -Intf $Intf -Reg 0x0B -Value $SlaveID

    # Aplicar la máscara eléctrica de prueba en GPIO
    Write-Reg -Intf $Intf -Reg 0x02 -Value 0x1F # Forzar pines 0 a 4 como salida
    Write-Reg -Intf $Intf -Reg 0x03 -Value $GpioState
    Start-Sleep -Milliseconds 5

    # Intentar escritura ciega en registro del sensor para forzar el estrobo de hardware
    Write-Reg -Intf $Intf -Reg 0x09 -Value 0x11 # Registro de prueba
    Write-Reg -Intf $Intf -Reg 0x0A -Value 0x00 # Datos nulos
    Write-Reg -Intf $Intf -Reg 0x10 -Value 0x11 # Lanzar escritura SIF + Start

    # Polling ultrarrápido del estrobo (Muestreo de 5 osclaciones)
    $Pkt = New-Object WINUSB_SETUP_PACKET
    $Pkt.RequestType = 0xC1; $Pkt.Request = 0x00; $Pkt.Value = 0x10; $Pkt.Length = 1
    $Buf = New-Object byte[] 1; [uint32]$Read = 0
    
    $Success = $false
    for ($i = 0; $i -lt 5; $i++) {
        $null = [WinUsbProber]::WinUsb_ControlTransfer($Intf, $Pkt, $Buf, 1, [ref]$Read, [IntPtr]::Zero)
        if ($Buf[0] -eq 0x00) {
            $Success = $true
            break
        }
        [System.Threading.Thread]::Sleep(1)
    }

    # Limpiar el bus de forma segura antes de la siguiente iteración
    Write-Reg -Intf $Intf -Reg 0x10 -Value 0x00
    return $Success
}

# 3. Ciclo de ejecución principal del escáner
$hFile = [IntPtr]::Zero; $WinusbHandle = [IntPtr]::Zero
try {
    # Tipado estricto de banderas Win32 para evitar desbordamientos en CreateFile
    $Access = [Convert]::ToUInt32('C0000000', 16) # GENERIC_READ | GENERIC_WRITE
    $Share = [uint32]3           # FILE_SHARE_READ | FILE_SHARE_WRITE
    $Disp = [uint32]3            # OPEN_EXISTING
    $Flags = [Convert]::ToUInt32('40000000', 16)  # FILE_FLAG_OVERLAPPED

    $hFile = [WinUsbProber]::CreateFile($CameraPath, $Access, $Share, [IntPtr]::Zero, $Disp, $Flags, [IntPtr]::Zero)
    if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) { throw "Dispositivo ocupado o sin permisos suficientes." }
    if (-not [WinUsbProber]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) { throw "Error al inicializar la interfaz WinUSB." }

    Write-Host '[+] Canal abierto con el Kernel. Escaneando combinaciones de hardware...' -ForegroundColor Green
    Write-Host '------------------------------------------------------------' -ForegroundColor Gray

    # Direcciones típicas de sensores en cámaras Genius/Sonix para priorizar
    $TargetSlaveIDs = @(0x11, 0x21, 0x30, 0x3A, 0x40, 0x48, 0x5D)
    
    # Rellenar el resto de direcciones posibles (0x00 a 0x7F)
    for ($id = 0x00; $id -le 0x7F; $id++) {
        if ($TargetSlaveIDs -notcontains $id) { $TargetSlaveIDs += $id }
    }

    $ValidMatches = @()

    # Barre registros GPIO (0x00 a 0x1F) y contrasta con los SlaveIDs
    for ($gpio = 0x00; $gpio -le 0x1F; $gpio++) {
        foreach ($slave in $TargetSlaveIDs) {
            if (Test-Probe -Intf $WinusbHandle -SlaveID $slave -GpioState $gpio) {
                Write-Host ("[¡ÉXITO!] Línea Respondona -> GPIO (Reg 0x03): 0x{0:X2} | Slave ID (Reg 0x0B): 0x{1:X2}" -f $gpio, $slave) -ForegroundColor Green
                $ValidMatches += [PSCustomObject]@{ GPIO = "0x{0:X2}" -f $gpio; SlaveID = "0x{0:X2}" -f $slave }
            }
        }
    }

    Write-Host '------------------------------------------------------------' -ForegroundColor Gray
    if ($ValidMatches.Count -gt 0) {
        Write-Host '[+] ESCANEO COMPLETADO: Hardware identificado con éxito.' -ForegroundColor Cyan
        $ValidMatches | Format-Table -AutoSize
    } else {
        Write-Host '[-] El bus SIF no arrojó respuestas válidas. Revisa el MCLK.' -ForegroundColor Red
    }
}
catch {
    Write-Host "[-] Error crítico durante el escaneo de registros: $_" -ForegroundColor Red
}
finally {
    # Liberación segura de punteros del sistema
    if ($WinusbHandle -ne [IntPtr]::Zero) { $null = [WinUsbProber]::WinUsb_Free($WinusbHandle) }
    if ($hFile -ne [IntPtr]::Zero -and $hFile.ToInt64() -ne -1) { $null = [WinUsbProber]::CloseHandle($hFile) }
    Write-Host '[+] Punteros e hilos de hardware liberados correctamente.' -ForegroundColor Gray
}