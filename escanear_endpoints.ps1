# =======================================================================
# UTILIDAD: ESCANER DE ENDPOINTS Y CONFIGURACIÓN DE BUS (WINUSB NATIVO)
# =======================================================================

if (-not ([System.Management.Automation.PSTypeName]'GeniusScanner.WinUsbPipeScanner').Type) {
    $Signature = @"
    using System;
    using System.Runtime.InteropServices;

    namespace GeniusScanner {
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct USB_INTERFACE_DESCRIPTOR {
            public byte bLength;
            public byte bDescriptorType;
            public byte bInterfaceNumber;
            public byte bAlternateSetting;
            public byte bNumEndpoints;
            public byte bInterfaceClass;
            public byte bInterfaceSubClass;
            public byte bInterfaceProtocol;
            public byte biInterface;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct WINUSB_PIPE_INFORMATION {
            public int PipeType; // USBD_PIPE_TYPE: 0=Control, 1=Isoch, 2=Bulk, 3=Interrupt
            public byte PipeId;  // Dirección del Endpoint (Ej: 0x81)
            public ushort MaximumPacketSize;
            public byte Interval;
        }

        public class WinUsbPipeScanner {
            [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
            public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_Initialize(IntPtr DeviceHandle, out IntPtr InterfaceHandle);

            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_QueryInterfaceSettings(IntPtr InterfaceHandle, byte AlternateSettingNumber, out USB_INTERFACE_DESCRIPTOR USBInterfaceDescriptor);

            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_QueryPipe(IntPtr InterfaceHandle, byte AlternateSettingNumber, byte PipeIndex, out WINUSB_PIPE_INFORMATION PipeInformation);

            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_Free(IntPtr InterfaceHandle);

            [DllImport("kernel32.dll", SetLastError = true)]
            public static extern bool CloseHandle(IntPtr hObject);
        }
    }
"@
    Add-Type -TypeDefinition $Signature
}

# --- Inicialización y Apertura del Canal del Dispositivo ---
$PnpDevice = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0C45&PID_60B0&MI_00*' } | Select-Object -First 1
if (-not $PnpDevice) { Write-Host '[-] Cámara Genius no detectada en el sistema.' -ForegroundColor Red; exit }
$CameraPath = '\\?\' + $PnpDevice.InstanceId.Replace('\', '#') + '#{dee824ef-729b-4a0e-9c14-b7117d33a817}'

$Access = [Convert]::ToUInt32('C0000000', 16) # GENERIC_READ | GENERIC_WRITE
$Share  = [uint32]3                           # FILE_SHARE_READ | FILE_SHARE_WRITE
$Disp   = [uint32]3                           # OPEN_EXISTING
$Flags  = [Convert]::ToUInt32('40000000', 16) # FILE_FLAG_OVERLAPPED

$hFile = [GeniusScanner.WinUsbPipeScanner]::CreateFile($CameraPath, $Access, $Share, [IntPtr]::Zero, $Disp, $Flags, [IntPtr]::Zero)
if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) { Write-Host "[-] No se pudo abrir el manejador del hardware." -ForegroundColor Red; exit }

$WinusbHandle = [IntPtr]::Zero
if (-not [GeniusScanner.WinUsbPipeScanner]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) { 
    Write-Host "[-] Falla crítica al inicializar WinUSB." -ForegroundColor Red
    $null = [GeniusScanner.WinUsbPipeScanner]::CloseHandle($hFile); exit 
}

try {
    Write-Host "=== ESCANEANDO INTERFAZ DE HARDWARE (ALTERNATE SETTING 1) ===" -ForegroundColor Magenta
    
    # 1. Interrogar la interfaz para obtener el descriptor general
    $InterfaceDesc = New-Object GeniusScanner.USB_INTERFACE_DESCRIPTOR
    if (-not [GeniusScanner.WinUsbPipeScanner]::WinUsb_QueryInterfaceSettings($WinusbHandle, 1, [ref]$InterfaceDesc)) {
        throw "El dispositivo no responde o no soporta el Alternate Setting 1 en su firmware."
    }

    Write-Host "[+] Conexión establecida con la interfaz número: $($InterfaceDesc.bInterfaceNumber)" -ForegroundColor Green
    Write-Host "[+] Cantidad de Endpoints activos detectados: $($InterfaceDesc.bNumEndpoints)`n" -ForegroundColor Green

    # Mapeo descriptivo del enumerador USBD_PIPE_TYPE
    $PipeTypes = @{ 0 = "Control"; 1 = "Isochronous (Streaming)"; 2 = "Bulk"; 3 = "Interrupt" }

    # 2. Iterar sobre cada PipeIndex disponible para extraer su configuración real
    for ($i = 0; $i -lt $InterfaceDesc.bNumEndpoints; $i++) {
        $PipeInfo = New-Object GeniusScanner.WINUSB_PIPE_INFORMATION
        if ([GeniusScanner.WinUsbPipeScanner]::WinUsb_QueryPipe($WinusbHandle, 1, $i, [ref]$PipeInfo)) {
            
            $HexId = "0x" + $PipeInfo.PipeId.ToString("X2")
            $FriendlyType = $PipeTypes[[int]$PipeInfo.PipeType]
            
            Write-Host "----------------────────────────────────────────" -ForegroundColor Gray
            Write-Host "Pipe Index       : $i" -ForegroundColor Cyan
            Write-Host "Endpoint Address : $HexId" -ForegroundColor Yellow
            Write-Host "Tipo de Tubería  : $FriendlyType" -ForegroundColor White
            Write-Host "wMaxPacketSize   : $($PipeInfo.MaximumPacketSize) Bytes" -ForegroundColor Green
            Write-Host "Intervalo de Bus : $($PipeInfo.Interval) ms/microframes" -ForegroundColor White
        }
    }
    Write-Host "----------------────────────────────────────────" -ForegroundColor Gray
}
catch {
    Write-Host "`n[-] Error durante el escaneo de tuberías: $_" -ForegroundColor Red
}
finally {
    # Cierre limpio de recursos
    if ($WinusbHandle -ne [IntPtr]::Zero) { $null = [GeniusScanner.WinUsbPipeScanner]::WinUsb_Free($WinusbHandle) }
    if ($hFile -ne [IntPtr]::Zero -and $hFile.ToInt64() -ne -1) { $null = [GeniusScanner.WinUsbPipeScanner]::CloseHandle($hFile) }
    Write-Host "`n[+] Escaneo finalizado. Conexiones cerradas." -ForegroundColor Gray
}