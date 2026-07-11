# =======================================================================
# 1. Registrar la infraestructura asincrona con Telemetria en la RAM
# =======================================================================
if (-not ([System.Management.Automation.PSTypeName]'WinUsbIsochTelemetry').Type) {
    $Signature = @"
    using System;
    using System.Runtime.InteropServices;

    public class WinUsbIsochTelemetry {
        [StructLayout(LayoutKind.Sequential)]
        public struct USBD_ISO_PACKET_DESCRIPTOR {
            public uint Offset;
            public uint Length;
            public uint Status;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct OVERLAPPED {
            public IntPtr Internal;
            public IntPtr InternalHigh;
            public int Offset;
            public int OffsetHigh;
            public IntPtr hEvent;
        }

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_Initialize(IntPtr DeviceHandle, out IntPtr InterfaceHandle);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_SetCurrentAlternateSetting(IntPtr InterfaceHandle, byte SettingNumber);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_RegisterIsochBuffer(IntPtr InterfaceHandle, byte PipeId, byte[] Buffer, uint BufferLength, out IntPtr IsochBufferHandle);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_ReadIsochPipe(IntPtr IsochBufferHandle, uint Offset, uint Length, ref uint FrameNumber, uint NumberOfPackets, [In, Out] USBD_ISO_PACKET_DESCRIPTOR[] IsoPacketDescriptors, ref OVERLAPPED Overlapped);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_GetOverlappedResult(IntPtr InterfaceHandle, ref OVERLAPPED Overlapped, out uint lpNumberOfBytesTransferred, bool bWait);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr CreateEvent(IntPtr lpEventAttributes, bool bManualReset, bool bInitialState, string lpName);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool ResetEvent(IntPtr hEvent);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_UnregisterIsochBuffer(IntPtr IsochBufferHandle);

        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_Free(IntPtr InterfaceHandle);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll")]
        public static extern ulong GetTickCount64();
    }
"@
    Add-Type -TypeDefinition $Signature
}

# =======================================================================
# 2. Configuracion de Variables Globales de Entorno y Descriptores
# =======================================================================
$CameraPath = "\\?\USB#VID_0C45&PID_60B0&MI_00#6&38F4DF72&0&0000#{dee824ef-729b-4a0e-9c14-b7117d33a817}"
$PipeName = "GeniusLookVideoPipe"

$hFile = [IntPtr]::Zero
$WinusbHandle = [IntPtr]::Zero
$IsochBufferHandle = [IntPtr]::Zero
$hEvent = [IntPtr]::Zero
$PipeClient = $null
$PipeWriter = $null

Write-Host "=== INICIANDO MOTOR ASINCRONO ISOCRONO CONTINUO ===" -ForegroundColor Cyan

try {
    Write-Host "[...] Conectando con el canal IPC del reproductor C++..." -ForegroundColor Yellow
    $PipeClient = New-Object System.IO.Pipes.NamedPipeClientStream(".", $PipeName, [System.IO.Pipes.PipeDirection]::Out)
    $PipeClient.Connect(5000)
    $PipeWriter = New-Object System.IO.BinaryWriter($PipeClient)
    Write-Host "[+] Canal IPC enlazado correctamente. Preparando streaming..." -ForegroundColor Green
} catch {
    Write-Host "[-] ERROR CRITICO: El reproductor.exe debe estar abierto ANTES de iniciar la captura." -ForegroundColor Red
    return
}

# =======================================================================
# 3. Inicializacion del Hardware y Configuracion del Bus Isocrono
# =======================================================================
try {
    $DesiredAccess = [Convert]::ToUInt32("C0000000", 16)
    $FlagsAttributes = [Convert]::ToUInt32("40000000", 16) # FILE_FLAG_OVERLAPPED
    
    $hFile = [WinUsbIsochTelemetry]::CreateFile($CameraPath, $DesiredAccess, 3, [IntPtr]::Zero, 3, $FlagsAttributes, [IntPtr]::Zero)

    if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) {
        throw "No se pudo abrir el descriptor fisico del dispositivo USB."
    }

    if (-not [WinUsbIsochTelemetry]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) {
        throw "Fallo la inicializacion de WinUSB."
    }
    
    if (-not [WinUsbIsochTelemetry]::WinUsb_SetCurrentAlternateSetting($WinusbHandle, 5)) {
        throw "El dispositivo rechazo el AltSetting 5."
    }
    
    Write-Host "[+] Canal Isocrono AltSetting 5 enlazado con exito." -ForegroundColor Green
    
    $NumPackets = 10
    $PacketSize = 680
    $TotalBufferSize = $NumPackets * $PacketSize
    $RawBuffer = New-Object byte[] $TotalBufferSize
    
    $Descriptors = New-Object WinUsbIsochTelemetry+USBD_ISO_PACKET_DESCRIPTOR[] $NumPackets
    for ($i = 0; $i -lt $NumPackets; $i++) {
        $Descriptors[$i].Offset = [uint32]($i * $PacketSize)
        $Descriptors[$i].Length = [uint32]$PacketSize
        $Descriptors[$i].Status = 0
    }

    if (-not [WinUsbIsochTelemetry]::WinUsb_RegisterIsochBuffer($WinusbHandle, 0x81, $RawBuffer, $TotalBufferSize, [ref]$IsochBufferHandle)) {
        throw "Fallo al registrar el buffer isocrono en DMA."
    }
    
    $Overlapped = New-Object WinUsbIsochTelemetry+OVERLAPPED
    $hEvent = [WinUsbIsochTelemetry]::CreateEvent([IntPtr]::Zero, $true, $false, $null)
    $Overlapped.hEvent = $hEvent
    
    [uint32]$FrameNumber = 0
    $KeepStreaming = $true
    
    # Telemetria en ASCII puro sin caracteres especiales
    $PacketsOk = 0
    $PacketsTimeout = 0
    $TotalBytesPS = 0
    $LastTick = [WinUsbIsochTelemetry]::GetTickCount64()

    Write-Host "[+] Transmitiendo frames en tiempo real. Presione [CTRL+C] para detener." -ForegroundColor Green
    
    # =======================================================================
    # 4. Bucle Principal con Telemetria Blindada
    # =======================================================================
    while ($KeepStreaming) {
        $Status = [WinUsbIsochTelemetry]::WinUsb_ReadIsochPipe($IsochBufferHandle, 0, $TotalBufferSize, [ref]$FrameNumber, $NumPackets, $Descriptors, [ref]$Overlapped)
        $LastError = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()

        if (-not $Status -and $LastError -eq 997) {
            $WaitResult = [WinUsbIsochTelemetry]::WaitForSingleObject($Overlapped.hEvent, 40)
            
            if ($WaitResult -eq 0) {
                [uint32]$BytesTransferidos = 0
                $null = [WinUsbIsochTelemetry]::WinUsb_GetOverlappedResult($WinusbHandle, [ref]$Overlapped, [ref]$BytesTransferidos, $true)
                
                if ($BytesTransferidos -gt 0) {
                    try {
                        $PipeWriter.Write($RawBuffer, 0, $BytesTransferidos)
                        $PipeClient.Flush()
                        $TotalBytesPS += $BytesTransferidos
                        $PacketsOk++
                    } catch {
                        Write-Host "[-] El reproductor cerro el canal IPC. Abortando stream." -ForegroundColor Red
                        $KeepStreaming = $false
                    }
                }
                $null = [WinUsbIsochTelemetry]::ResetEvent($Overlapped.hEvent)
            } else {
                $PacketsTimeout++
                $null = [WinUsbIsochTelemetry]::ResetEvent($Overlapped.hEvent)
            }
        } else {
            Write-Host "[-] Error critico de transferencia E/S en el bus USB. Codigo: $LastError" -ForegroundColor Red
            $KeepStreaming = $false
        }

        # Calculo de rendimiento cada 1000ms
        $CurrentTick = [WinUsbIsochTelemetry]::GetTickCount64()
        if ($CurrentTick - $LastTick -ge 1000) {
            $MBPS = [Math]::Round(($TotalBytesPS / (1024 * 1024)), 2)
            Write-Host -NoNewline ("`r[HW_TELEMETRY] USB -> RAM: $MBPS MB/s | OK: $PacketsOk | TIMEOUTS: $PacketsTimeout" + "    ") -ForegroundColor Yellow
            
            $TotalBytesPS = 0
            $PacketsOk = 0
            $PacketsTimeout = 0
            $LastTick = $CurrentTick
        }
    }
}
catch {
    Write-Host "`n[-] Excepcion controlada en ejecucion: $_" -ForegroundColor Red
}
finally {
    # =======================================================================
    # 5. Capa Rigida de Desconexion y Liberacion de Memoria
    # =======================================================================
    Write-Host "`n[...] Liberando descriptores y cerrando compuertas de hardware..." -ForegroundColor Yellow
    if ($PipeWriter) { $PipeWriter.Close(); $PipeWriter.Dispose() }
    if ($PipeClient) { $PipeClient.Close(); $PipeClient.Dispose() }
    if ($hEvent -ne [IntPtr]::Zero) { $null = [WinUsbIsochTelemetry]::CloseHandle($hEvent) }
    if ($IsochBufferHandle -ne [IntPtr]::Zero) { $null = [WinUsbIsochTelemetry]::WinUsb_UnregisterIsochBuffer($IsochBufferHandle) }
    if ($WinusbHandle -ne [IntPtr]::Zero) { $null = [WinUsbIsochTelemetry]::WinUsb_Free($WinusbHandle) }
    if ($hFile -ne [IntPtr]::Zero -and $hFile.ToInt64() -ne -1) { $null = [WinUsbIsochTelemetry]::CloseHandle($hFile) }
    Write-Host "[+] Limpieza finalizada. Canal de hardware en reposo." -ForegroundColor Green
}