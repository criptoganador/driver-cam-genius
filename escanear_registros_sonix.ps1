# =======================================================================
# 1. Registro de Estructuras Nativas para Probing de Registros (WinUSB)
# =======================================================================
if (-not ([System.Management.Automation.PSTypeName]'WinUsbScanner').Type) {
    $Signature = @"
    using System;
    using System.Runtime.InteropServices;

    public class WinUsbScanner {
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

Write-Host '=== INICIANDO ESCANEO DE MAPA DE REGISTROS (SONIX ASIC) ===' -ForegroundColor Cyan

# =======================================================================
# 2. Motor de Descubrimiento Dinámico de Rutas PnP
# =======================================================================
Write-Host '[...] Localizando la instancia activa de la Genius Look en el bus PnP...' -ForegroundColor Yellow
$PnpDevice = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0C45&PID_60B0&MI_00*' } | Select-Object -First 1

if (-not $PnpDevice) {
    Write-Host '[-] CRÍTICO: Dispositivo no encontrado en el bus PnP. Conecta la camara.' -ForegroundColor Red
    return
}

$RawInstanceId = $PnpDevice.InstanceId
$SanitizedInstance = $RawInstanceId.Replace('\', '#')
$CameraPath = '\\?\' + $SanitizedInstance + '#{dee824ef-729b-4a0e-9c14-b7117d33a817}'

Write-Host ('[+] Instancia activa acoplada: ' + $RawInstanceId) -ForegroundColor Gray

$hFile = [IntPtr]::Zero
$WinusbHandle = [IntPtr]::Zero

# =======================================================================
# 3. Helper de Lectura de Registros via Tubería IN (ControlTransfer)
# =======================================================================
function Read-SonixRegister {
    param (
        [IntPtr]$InterfaceHandle,
        [uint16]$Register
    )

    $SetupPacket = New-Object WinUsbScanner+WINUSB_SETUP_PACKET
    # 0xC1 = Dirección: IN (Dispositivo a Host) | Tipo: Vendor | Destinatario: Interface
    $SetupPacket.RequestType = 0xC1 
    $SetupPacket.Request = 0x00     # bRequest 0x00: Comando de Lectura estándar Sonix
    $SetupPacket.Value = $Register  
    $SetupPacket.Index = 0x0000
    $SetupPacket.Length = 1

    $Buffer = New-Object byte[] 1
    [uint32]$BytesTransferred = 0

    $Status = [WinUsbScanner]::WinUsb_ControlTransfer($InterfaceHandle, $SetupPacket, $Buffer, 1, [ref]$BytesTransferred, [IntPtr]::Zero)
    if (-not $Status) {
        $ErrorId = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw ('Error al leer Registro 0x' + ('{0:X2}' -f $Register) + '. Codigo Win32: ' + $ErrorId)
    }
    return $Buffer[0]
}

# =======================================================================
# 4. Flujo de Diagnóstico Principal (Try-Catch-Finally)
# =======================================================================
try {
    $DesiredAccess = [Convert]::ToUInt32('C0000000', 16) 
    $FlagsAttributes = [Convert]::ToUInt32('40000000', 16) 
    
    $hFile = [WinUsbScanner]::CreateFile($CameraPath, $DesiredAccess, 3, [IntPtr]::Zero, 3, $FlagsAttributes, [IntPtr]::Zero)

    if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) {
        $ErrorKernel = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw ('No se pudo abrir el canal fisico de la camara. Codigo Win32: ' + $ErrorKernel)
    }

    if (-not [WinUsbScanner]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) {
        $LastError = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw ('No se pudo inicializar la interfaz WinUSB. Codigo Win32: ' + $LastError)
    }

    Write-Host '[+] Canal WinUSB abierto. Leyendo estado del silicio...' -ForegroundColor Green
    Write-Host '-------------------------------------------------------------' -ForegroundColor Gray

    # Mapeo estratégico de los registros modificados en la inicialización
    $TargetRegisters = @(0x01, 0x02, 0x08, 0x09, 0x0A, 0x0B, 0x10)
    
    foreach ($Reg in $TargetRegisters) {
        $Val = Read-SonixRegister -InterfaceHandle $WinusbHandle -Register $Reg
        $RegHex = '0x{0:X2}' -f $Reg
        $ValHex = '0x{0:X2}' -f $Val
        
        # Inyección de metadatos de arquitectura en el log
        $Context = ''
        if ($Reg -eq 0x01) { $Context = '-> Control de Clock (Debe estar en 0x00)' }
        elseif ($Reg -eq 0x08) { $Context = '-> Transmision SIF (Debe estar en 0x1A)' }
        elseif ($Reg -eq 0x0B) { $Context = '-> ID Esclavo CMOS (Debe estar en 0x21)' }
        
        Write-Host ("[REG] $RegHex : $ValHex $Context") -ForegroundColor Gray
    }

    Write-Host '-------------------------------------------------------------' -ForegroundColor Gray
    Write-Host '[+++] ESCANEO DE REGISTROS COMPLETADO CON EXITO.' -ForegroundColor Green
}
catch {
    Write-Host ('[-] ERROR DURANTE EL PROBING: ' + $_) -ForegroundColor Red
}
finally {
    # El bloque finally asegura la liberación del descriptor pase lo que pase
    if ($WinusbHandle -ne [IntPtr]::Zero) { $null = [WinUsbScanner]::WinUsb_Free($WinusbHandle) }
    if ($hFile -ne [IntPtr]::Zero -and $hFile.ToInt64() -ne -1) { $null = [WinUsbScanner]::CloseHandle($hFile) }
    Write-Host '[+] Handle de control cerrado limpiamente.' -ForegroundColor Green
}