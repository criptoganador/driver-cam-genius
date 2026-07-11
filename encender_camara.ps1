# =======================================================================
# 1. Registrar la infraestructura de comandos de control en la RAM
# =======================================================================
if (-not ([System.Management.Automation.PSTypeName]'WinUsbControl').Type) {
    $Signature = @"
    using System;
    using System.Runtime.InteropServices;

    public class WinUsbControl {
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

Write-Host '=== INICIALIZANDO ARRANQUE DE SENSOR VIA WINUSB ===' -ForegroundColor Cyan

# =======================================================================
# 2. Motor de Descubrimiento Dinámico de Rutas PnP (Evita rutas estáticas)
# =======================================================================
Write-Host '[...] Interrogando al bus PnP de Windows por la interfaz MI_00...' -ForegroundColor Yellow
$PnpDevice = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0C45&PID_60B0&MI_00*' } | Select-Object -First 1

if (-not $PnpDevice) {
    Write-Host '[-] CRÍTICO: La cámara no está conectada o el driver winusb.sys no está asociado a MI_00.' -ForegroundColor Red
    Write-Host '[i] Verifica el Administrador de Dispositivos.' -ForegroundColor DarkYellow
    return
}

# Traducimos el InstanceId del kernel al formato de enlace simbólico WinUSB
$RawInstanceId = $PnpDevice.InstanceId
$SanitizedInstance = $RawInstanceId.Replace('\', '#')
$RutaCamara = '\\?\' + $SanitizedInstance + '#{dee824ef-729b-4a0e-9c14-b7117d33a817}'

Write-Host ('[+] Dispositivo detectado: ' + $RawInstanceId) -ForegroundColor Gray
Write-Host '[+] Enlace simbólico generado dinámicamente con éxito.' -ForegroundColor Green

# =======================================================================
# 3. Apertura del Descriptor de Comunicación y Control de Erreores Nativo
# =======================================================================
$DesiredAccess = [Convert]::ToUInt32('C0000000', 16)
$FlagsAttributes = [Convert]::ToUInt32('40000000', 16)

$hFile = [WinUsbControl]::CreateFile($RutaCamara, $DesiredAccess, 3, [IntPtr]::Zero, 3, $FlagsAttributes, [IntPtr]::Zero)

if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) {
    $ErrorKernel = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Write-Host ('[-] Error de comunicacion con el puerto fisico. Codigo Win32: ' + $ErrorKernel) -ForegroundColor Red
    
    if ($ErrorKernel -eq 5) {
        Write-Host '[!] Alerta: Acceso Denegado. El puerto está bloqueado por otra aplicación (ej. el reproductor o un proceso colgado).' -ForegroundColor Yellow
    } elseif ($ErrorKernel -eq 2) {
        Write-Host '[!] Alerta: Archivo no encontrado. La ruta generada no coincide con el registro activo del sistema.' -ForegroundColor Yellow
    }
    return
}

$WinusbHandle = [IntPtr]::Zero
if ([WinUsbControl]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) {
    
    $SetupPacket = New-Object WinUsbControl+WINUSB_SETUP_PACKET
    $SetupPacket.RequestType = 0x41  
    $SetupPacket.Request     = 0x08  
    $SetupPacket.Value       = 0x0001 
    $SetupPacket.Index       = 0x0000 
    $SetupPacket.Length      = 1      

    $DataBuffer = New-Object byte[] 1
    $DataBuffer[0] = 0x00 
    $LengthTransferred = 0

    Write-Host '[...] Transmitiendo payload al Registro [0x01] mediante Data Stage...' -ForegroundColor Yellow

    if ([WinUsbControl]::WinUsb_ControlTransfer($WinusbHandle, $SetupPacket, $DataBuffer, 1, [ref]$LengthTransferred, [IntPtr]::Zero)) {
        Write-Host '[+] EXITO EN KERNEL: El chip Sonix acepto la estructura de datos.' -ForegroundColor Green
        Write-Host '[+] El oscilador clock del sensor CMOS ha despertado con exito.' -ForegroundColor Cyan
    } else {
        $ErrorWin32 = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Host ('[-] El hardware rechazo el paquete. Codigo de error: ' + $ErrorWin32) -ForegroundColor Red
    }

    $null = [WinUsbControl]::WinUsb_Free($WinusbHandle)
}

$null = [WinUsbControl]::CloseHandle($hFile)
Write-Host '=== Operacion de control finalizada ===' -ForegroundColor Cyan