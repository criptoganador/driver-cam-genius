# =======================================================================
# 1. Registro de Infraestructura de Control Nativa
# =======================================================================
if (-not ([System.Management.Automation.PSTypeName]'WinUsbVerifier').Type) {
    $Signature = @"
    using System;
    using System.Runtime.InteropServices;

    public class WinUsbVerifier {
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct WINUSB_SETUP_PACKET {
            public byte RequestType;
            public byte Request;
            public ushort Value;
            public ushort Index;
            public ushort Length;
        }

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

Write-Host '=== INICIANDO CANAL DE VERIFICACIÓN EN CALIENTE ===' -ForegroundColor Cyan

# =======================================================================
# 2. Enrutamiento PnP Dinámico
# =======================================================================
$PnpDevice = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0C45&PID_60B0&MI_00*' } | Select-Object -First 1

if (-not $PnpDevice) {
    Write-Host '[-] CRÍTICO: Dispositivo físico no detectado en el bus USB.' -ForegroundColor Red
    return
}

$RawInstanceId = $PnpDevice.InstanceId
$SanitizedInstance = $RawInstanceId.Replace('\', '#')
$CameraPath = '\\?\' + $SanitizedInstance + '#{dee824ef-729b-4a0e-9c14-b7117d33a817}'

$hFile = [IntPtr]::Zero
$WinusbHandle = [IntPtr]::Zero

# =======================================================================
# 3. Bloque de Ejecución Segura con Captura de Erreores Win32
# =======================================================================
try {
    $DesiredAccess = [Convert]::ToUInt32('C0000000', 16) 
    $FlagsAttributes = [Convert]::ToUInt32('40000000', 16) 
    
    $hFile = [WinUsbVerifier]::CreateFile($CameraPath, $DesiredAccess, 3, [IntPtr]::Zero, 3, $FlagsAttributes, [IntPtr]::Zero)

    if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) {
        $ErrorKernel = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Descriptor inalcanzable. Código Win32 nativo: $ErrorKernel"
    }

    if (-not [WinUsbVerifier]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) {
        $LastError = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Fallo de acople WinUSB. Código Win32: $LastError"
    }

    Write-Host '[+] Canal acoplado. Ejecutando Read-Back inmediato...' -ForegroundColor Green
    Write-Host '-------------------------------------------------------------' -ForegroundColor Gray

    # Estructura del paquete de lectura vendor (0xC1)
    $SetupPacket = New-Object WinUsbVerifier+WINUSB_SETUP_PACKET
    $SetupPacket.RequestType = 0xC1
    $SetupPacket.Request     = 0x00
    $SetupPacket.Index       = 0x0000
    $SetupPacket.Length      = 1
    $Buffer = New-Object byte[] 1
    $BytesTransferred = 0

    # Lectura del mapa crítico
    $Targets = @{ 0x01="Clock Control"; 0x08="SIF Mode"; 0x0B="CMOS Slave ID" }
    
    foreach ($Reg in $Targets.Keys) {
        $SetupPacket.Value = $Reg
        if ([WinUsbVerifier]::WinUsb_ControlTransfer($WinusbHandle, $SetupPacket, $Buffer, 1, [ref]$BytesTransferred, [IntPtr]::Zero)) {
            $HexReg = '0x{0:X2}' -f $Reg
            $HexVal = '0x{0:X2}' -f $Buffer[0]
            Write-Host "[REG] $HexReg ($($Targets[$Reg])) -> Valor en Silicio: $HexVal" -ForegroundColor Gray
        }
    }
    Write-Host '-------------------------------------------------------------' -ForegroundColor Gray

} catch {
    Write-Host "[-] ERRORES EN KERNEL: $_" -ForegroundColor Red
    if ($_ -like "*Código Win32 nativo: 5*") {
        Write-Host '[!] DIAGNÓSTICO: El puerto está retenido (Access Denied). Windows no ha liberado el hilo anterior.' -ForegroundColor Yellow
    } elseif ($_ -like "*Código Win32 nativo: 2*") {
        Write-Host '[!] DIAGNÓSTICO: Dispositivo no encontrado (File Not Found). Comprueba la conexión física.' -ForegroundColor Yellow
    }
} finally {
    # Liberación estricta de punteros
    if ($WinusbHandle -ne [IntPtr]::Zero) { $null = [WinUsbVerifier]::WinUsb_Free($WinusbHandle) }
    if ($hFile -ne [IntPtr]::Zero -and $hFile.ToInt64() -ne -1) { $null = [WinUsbVerifier]::CloseHandle($hFile) }
    Write-Host '[+] Canal de diagnóstico cerrado limpiamente.' -ForegroundColor Green
}