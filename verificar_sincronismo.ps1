<#
.SYNOPSIS
    Script de diagnóstico de bajo nivel para verificar la actividad de sincronismo
    entre el puente Sonix y el sensor PixArt PAS106.
#>

# 1. DEFINICIÓN DE ESTRUCTURAS NATIVAS DE WINUSB
$Signature = @'
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

public class WinUsbNative {
    [DllImport("winusb.dll", SetLastError = true)]
    public static extern bool WinUsb_ControlTransfer(
        IntPtr InterfaceHandle,
        WINUSB_SETUP_PACKET SetupPacket,
        byte[] Buffer,
        uint BufferLength,
        ref uint LengthTransferred,
        IntPtr Overlapped
    );
}
'@

if (-not ([Object].GetInterface("WinUsbNative"))) {
    Add-Type -TypeDefinition $Signature
}

# 2. CAPA DE ABSTRACCIÓN DE HARDWARE (READ REGISTER)
function Read-SonixRegister {
    param (
        [IntPtr]$InterfaceHandle,
        [byte]$Register
    )

    $SetupPacket = New-Object WINUSB_SETUP_PACKET
    $SetupPacket.RequestType = 0xC1 # Device-to-Host | Vendor | Interface
    $SetupPacket.Request     = 0x00 # Comando genérico de lectura de registros Sonix
    $SetupPacket.Value       = [uint16]$Register
    $SetupPacket.Index       = 0x0000
    $SetupPacket.Length      = 1

    $Buffer = New-Object byte[] 1
    $transferred = 0

    $result = [WinUsbNative]::WinUsb_ControlTransfer(
        $InterfaceHandle,
        $SetupPacket,
        $Buffer,
        1,
        [ref]$transferred,
        [IntPtr]::Zero
    )

    if (-not $result) {
        throw "Error de hardware al leer Registro 0x$($Register.ToString('X2'))"
    }

    return $Buffer[0]
}

# 3. FUNCIÓN PRINCIPAL DE DIAGNÓSTICO (CORREGIDA SIN ADVERTENCIAS DE LINTER)
function Test-SonixSyncSignals {
    param (
        [IntPtr]$WinUsbHandle
    )

    Write-Host "`n=== INICIANDO MONITOREO DE SENALES DE HARDWARE ===" -ForegroundColor Cyan
    Write-Host "Tomando 10 muestras de control en tiempo real..." -ForegroundColor Yellow

    $SamplesReg05 = [System.Collections.Generic.List[byte]]::new()
    $SamplesReg08 = [System.Collections.Generic.List[byte]]::new()

    try {
        for ($i = 0; $i -lt 10; $i++) {
            # Leer el registro de estado de frame/DMA y el estado del bus SIF
            $val05 = Read-SonixRegister -InterfaceHandle $WinUsbHandle -Register 0x05
            $val08 = Read-SonixRegister -InterfaceHandle $WinUsbHandle -Register 0x08

            $SamplesReg05.Add($val05)
            $SamplesReg08.Add($val08)

            Write-Host "Muestra [$(($i+1).ToString('D2'))] -> Reg 0x05 (Frame/Status): 0x$($val05.ToString('X2')) | Reg 0x08 (SIF Bus): 0x$($val08.ToString('X2'))"
            
            # Delay corto para permitir que el hardware cambie de estado entre ciclos de reloj
            Start-Sleep -Milliseconds 50
        }

        # ANALIZAR VARIANZA (¿Los datos cambian o están muertos?)
        $DistinctReg05 = ($SamplesReg05 | Select-Object -Unique).Count

        Write-Host "`n=== DIAGNOSTICO FINAL ===" -ForegroundColor Cyan

        # Evaluación del Reloj de Píxel y VSYNC
        if ($DistinctReg05 -gt 1) {
            Write-Host "[+] PASO: Se detecta fluctuacion en Reg 0x05. El motor de sincronismo esta procesando datos dinamicos." -ForegroundColor Green
        } else {
            Write-Host "[-] FALLO: Reg 0x05 esta congelado en 0x$($SamplesReg05[0].ToString('X2')). El sensor NO esta enviando pulsos VSYNC/PCLK o el puente los ignora." -ForegroundColor Red
        }

        # Evaluación del estado del Bus SIF (Usando la última muestra recolectada)
        $LastReg08 = $SamplesReg08[-1]
        if (($LastReg08 -band 0x01) -eq 0x01) {
            Write-Host "[!] ADVERTENCIA: El Bit 0 del Reg 0x08 esta bloqueado en 1 (SIF BUSY). El bus fisico colapso electricamente." -ForegroundColor Red
        } else {
            Write-Host "[+] PASO: El bus SIF esta libre (Idle) para recibir comandos." -ForegroundColor Green
        }

    } catch {
        Write-Host "[-] Excepcion en el diagnostico: $_" -ForegroundColor Red
    }
}

# 4. INSTANCIACIÓN Y LLAMADA AUTOMÁTICA
$PnpDevice = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0C45&PID_60B0&MI_00*' } | Select-Object -First 1
if (-not $PnpDevice) {
    Write-Host "[-] Dispositivo PnP no encontrado." -ForegroundColor Red
    exit
}

$CameraPath = '\\?\' + $PnpDevice.InstanceId.Replace('\', '#') + '#{dee824ef-729b-4a0e-9c14-b7117d33a817}'

$SignatureMain = @"
using System;
using System.Runtime.InteropServices;
public class WinUsbProberExt {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);
    [DllImport("winusb.dll", SetLastError = true)]
    public static extern bool WinUsb_Initialize(IntPtr DeviceHandle, out IntPtr InterfaceHandle);
    [DllImport("winusb.dll", SetLastError = true)]
    public static extern bool WinUsb_Free(IntPtr InterfaceHandle);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);
}
"@
if (-not ([System.Management.Automation.PSTypeName]'WinUsbProberExt').Type) {
    Add-Type -TypeDefinition $SignatureMain
}

$hFile = [WinUsbProberExt]::CreateFile($CameraPath, [Convert]::ToUInt32('C0000000', 16), 3, [IntPtr]::Zero, 3, [Convert]::ToUInt32('40000000', 16), [IntPtr]::Zero)
if ($hFile -ne [IntPtr]::Zero -and $hFile.ToInt64() -ne -1) {
    $WinusbHandle = [IntPtr]::Zero
    if ([WinUsbProberExt]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) {
        Test-SonixSyncSignals -WinUsbHandle $WinusbHandle
        $null = [WinUsbProberExt]::WinUsb_Free($WinusbHandle)
    }
    $null = [WinUsbProberExt]::CloseHandle($hFile)
} else {
    Write-Host "[-] No se pudo abrir el descriptor del dispositivo." -ForegroundColor Red
}