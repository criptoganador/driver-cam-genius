# =======================================================================
# FASE 4 (PRODUCCION): CAPTURA ISOCRONA EN ALINEACION DE BUS DE 128 BYTES
# =======================================================================

# Forzar UTF-8 en la consola para evitar caracteres corruptos
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

if (-not ([System.Management.Automation.PSTypeName]'GeniusFinal.WinUsbFinalEngine').Type) {
    $Signature = @"
    using System;
    using System.Runtime.InteropServices;

    namespace GeniusFinal {
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct WINUSB_SETUP_PACKET {
            public byte RequestType;
            public byte Request;
            public ushort Value;
            public ushort Index;
            public ushort Length;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct OVERLAPPED {
            public IntPtr Internal;
            public IntPtr InternalHigh;
            public uint Offset;
            public uint OffsetHigh;
            public IntPtr hEvent;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct WINUSB_ISOCH_PACKET_DESCRIPTOR {
            public uint Offset;
            public uint Length;
            public uint Status;
        }

        public class WinUsbFinalEngine {
            [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
            public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);
            
            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_Initialize(IntPtr DeviceHandle, out IntPtr InterfaceHandle);
            
            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_SetCurrentAlternateSetting(IntPtr InterfaceHandle, byte SettingNumber);
            
            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_ControlTransfer(IntPtr InterfaceHandle, WINUSB_SETUP_PACKET SetupPacket, byte[] Buffer, uint BufferLength, out uint LengthTransferred, IntPtr Overlapped);
            
            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_RegisterIsochBuffer(IntPtr InterfaceHandle, byte PipeID, byte[] Buffer, uint BufferLength, out IntPtr IsochBufferHandle);

            // FIRMA CORREGIDA: ref FrameNumber es parametro de ENTRADA (frame de inicio)
            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_ReadIsochPipe(IntPtr IsochBufferHandle, uint Offset, uint Length, ref uint FrameNumber, uint NumberOfPackets, [In, Out] WINUSB_ISOCH_PACKET_DESCRIPTOR[] PacketDescriptors, ref OVERLAPPED Overlapped);

            // Necesario para scheduling de frames: obtiene el frame USB actual del controlador de host
            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_GetCurrentFrameNumber(IntPtr InterfaceHandle, out uint CurrentFrameNumber, out long TimeStamp);

            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_GetOverlappedResult(IntPtr InterfaceHandle, ref OVERLAPPED Overlapped, out uint lpNumberOfBytesTransferred, bool bWait);

            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_UnregisterIsochBuffer(IntPtr IsochBufferHandle);

            [DllImport("winusb.dll", SetLastError = true)]
            public static extern bool WinUsb_Free(IntPtr InterfaceHandle);
            
            [DllImport("kernel32.dll", SetLastError = true)]
            public static extern bool CloseHandle(IntPtr hObject);

            [DllImport("kernel32.dll", SetLastError = true)]
            public static extern IntPtr CreateEvent(IntPtr lpEventAttributes, bool bManualReset, bool bInitialState, string lpName);

            public static int LeerStreamingIsoch(IntPtr intfHandle, byte pipeId, byte[] rawBuffer, uint pktSize, uint numPkts, out int kernelError) {
                kernelError = 0;
                IntPtr isochBufferHandle = IntPtr.Zero;
                uint totalLength = pktSize * numPkts;
                
                if (!WinUsb_RegisterIsochBuffer(intfHandle, pipeId, rawBuffer, totalLength, out isochBufferHandle)) {
                    kernelError = Marshal.GetLastWin32Error();
                    return -1;
                }

                IntPtr hEvt = CreateEvent(IntPtr.Zero, true, false, null);
                OVERLAPPED ovr = new OVERLAPPED();
                ovr.hEvent = hEvt;

                // Obtener frame actual y programar 8 frames adelante (safe scheduling)
                uint currentFrame = 0;
                long ts = 0;
                if (!WinUsb_GetCurrentFrameNumber(intfHandle, out currentFrame, out ts)) {
                    currentFrame = 10; // fallback si la llamada falla
                }
                uint startFrame = currentFrame + 8;

                WINUSB_ISOCH_PACKET_DESCRIPTOR[] descs = new WINUSB_ISOCH_PACKET_DESCRIPTOR[numPkts];
                for (uint i = 0; i < numPkts; i++) {
                    descs[i].Offset = i * pktSize;
                    descs[i].Length = pktSize;
                    descs[i].Status = 0;
                }

                bool success = WinUsb_ReadIsochPipe(isochBufferHandle, 0, totalLength, ref startFrame, numPkts, descs, ref ovr);
                uint bytesTransferred = 0;

                if (!success) {
                    int errorCode = Marshal.GetLastWin32Error();
                    if (errorCode == 997) {
                        if (WinUsb_GetOverlappedResult(intfHandle, ref ovr, out bytesTransferred, true)) {
                            int finalBytes = (int)bytesTransferred;
                            CloseHandle(hEvt);
                            WinUsb_UnregisterIsochBuffer(isochBufferHandle);
                            return finalBytes;
                        }
                    }
                    kernelError = Marshal.GetLastWin32Error();
                    bytesTransferred = 0;
                } else {
                    WinUsb_GetOverlappedResult(intfHandle, ref ovr, out bytesTransferred, true);
                }

                CloseHandle(hEvt);
                WinUsb_UnregisterIsochBuffer(isochBufferHandle);
                return (int)bytesTransferred;
            }
        }
    }
"@
    Add-Type -TypeDefinition $Signature
}

function Write-CoreReg ([IntPtr]$Intf, [uint16]$Reg, [byte]$Val) {
    $Pkt = New-Object GeniusFinal.WINUSB_SETUP_PACKET
    $Pkt.RequestType = 0x41; $Pkt.Request = 0x08; $Pkt.Value = $Reg; $Pkt.Length = 1
    $Buf = New-Object byte[] 1; $Buf[0] = $Val; [uint32]$Transferred = 0
    $null = [GeniusFinal.WinUsbFinalEngine]::WinUsb_ControlTransfer($Intf, $Pkt, $Buf, 1, [ref]$Transferred, [IntPtr]::Zero)
}

function Write-CoreRegBlock ([IntPtr]$Intf, [uint16]$Reg, [byte[]]$Buf) {
    $Pkt = New-Object GeniusFinal.WINUSB_SETUP_PACKET
    $Pkt.RequestType = 0x41; $Pkt.Request = 0x08; $Pkt.Value = $Reg; $Pkt.Length = [uint16]$Buf.Length
    [uint32]$Transferred = 0
    $null = [GeniusFinal.WinUsbFinalEngine]::WinUsb_ControlTransfer($Intf, $Pkt, $Buf, [uint32]$Buf.Length, [ref]$Transferred, [IntPtr]::Zero)
}

function Write-SensorRegister ([IntPtr]$Intf, [byte]$Register, [byte]$Value) {
    # Protocolo SIF de 3 pasos via registros del puente Sonix (el unico que funciona)
    # 1. Cargar la direccion del registro destino del sensor
    Write-CoreReg -Intf $Intf -Reg 0x09 -Value $Register
    # 2. Cargar el valor a inyectar
    Write-CoreReg -Intf $Intf -Reg 0x0A -Value $Value
    # 3. Disparar el estrobo de hardware (0x11 = Write 1 Byte + SIF Start)
    Write-CoreReg -Intf $Intf -Reg 0x10 -Value 0x11

    # 4. Polling: esperar a que el estrobo se limpie (el hardware lo pone a 0x00 al terminar)
    $PktRead = New-Object GeniusFinal.WINUSB_SETUP_PACKET
    $PktRead.RequestType = 0xC1; $PktRead.Request = 0x00; $PktRead.Value = 0x10; $PktRead.Length = 1
    $BufRead = New-Object byte[] 1; [uint32]$Read = 0

    $StrobeCleared = $false
    for ($retry = 0; $retry -lt 20; $retry++) {
        $null = [GeniusFinal.WinUsbFinalEngine]::WinUsb_ControlTransfer($Intf, $PktRead, $BufRead, 1, [ref]$Read, [IntPtr]::Zero)
        if ($BufRead[0] -eq 0x00) {
            $StrobeCleared = $true
            break
        }
        [System.Threading.Thread]::Sleep(2)
    }

    if (-not $StrobeCleared) {
        Write-Host "[-] Timeout SIF Reg 0x$('{0:X2}' -f $Register) (estrobo atascado en 0x$('{0:X2}' -f $BufRead[0]))" -ForegroundColor Yellow
    }
}

$PnpDevice = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0C45&PID_60B0&MI_00*' } | Select-Object -First 1
if (-not $PnpDevice) { Write-Host '[-] Hardware Genius no detectado.' -ForegroundColor Red; exit }
$CameraPath = '\\?\' + $PnpDevice.InstanceId.Replace('\', '#') + '#{dee824ef-729b-4a0e-9c14-b7117d33a817}'

$Access = [Convert]::ToUInt32('C0000000', 16)
$Share  = [uint32]3
$Disp   = [uint32]3
$Flags  = [Convert]::ToUInt32('40000000', 16)

$hFile = [GeniusFinal.WinUsbFinalEngine]::CreateFile($CameraPath, $Access, $Share, [IntPtr]::Zero, $Disp, $Flags, [IntPtr]::Zero)
if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) { Write-Host '[-] Falla al abrir descriptor de hardware.' -ForegroundColor Red; exit }

$WinusbHandle = [IntPtr]::Zero
if (-not [GeniusFinal.WinUsbFinalEngine]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) {
    Write-Host '[-] Error al instanciar subsistema WinUSB.' -ForegroundColor Red
    $null = [GeniusFinal.WinUsbFinalEngine]::CloseHandle($hFile); exit
}

try {
    Write-Host '=== CONFIGURANDO PARAMETROS FISICOS CONFIRMADOS ===' -ForegroundColor Magenta

    if (-not [GeniusFinal.WinUsbFinalEngine]::WinUsb_SetCurrentAlternateSetting($WinusbHandle, 1)) {
        throw 'Rechazo del bus al conmutar al Alternate Setting 1.'
    }
    Write-Host '[+] Alternate Setting 1 activo en controlador de host.' -ForegroundColor Green

    # --- SECUENCIA MAESTRA INTEGRAD (DESPUÉS DEL ALT SETTING) ---
    Write-Host '[...] Configurando puente Sonix (bloque 25 bytes)...' -ForegroundColor Gray
    $InitBlock = [byte[]](
        0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x04, 0x01, 0x00, 0x16, 0x12, 0x24, 0x86, 0x2b
    )
    Write-CoreRegBlock -Intf $WinusbHandle -Reg 0x01 -Buf $InitBlock

    # Estabilizacion galvanica del oscilador del sensor (obligatorio)
    Start-Sleep -Milliseconds 35

    # Configurar velocidad segura del bus SIF y direccion del esclavo PAS106
    # (el InitBlock deja Reg 0x0B = 0x00, sin slave address -> causa todos los fallos SIF)
    Write-Host '[...] Configurando bus SIF: velocidad segura + slave address 0x40...' -ForegroundColor Gray
    Write-CoreReg -Intf $WinusbHandle -Reg 0x08 -Value 0x14  # SIF clock divider seguro
    Write-CoreReg -Intf $WinusbHandle -Reg 0x0B -Value 0x40  # Slave address del PAS106B

    Write-Host '[...] Inyectando secuencias operativas en la matriz PixArt (PAS106)...' -ForegroundColor Cyan
    # Tabla de registros correcta del PAS106B (extraida de inicializar_sensor_sif.ps1 probado)
    Write-SensorRegister -Intf $WinusbHandle -Register 0x02 -Value 0x0C # Modo y reloj interno
    Write-SensorRegister -Intf $WinusbHandle -Register 0x03 -Value 0x40 # Pixel Clock Polarity
    Write-SensorRegister -Intf $WinusbHandle -Register 0x04 -Value 0x05 # Modo ventana de captura
    Write-SensorRegister -Intf $WinusbHandle -Register 0x05 -Value 0x24 # Supresion de ruido termico
    Write-SensorRegister -Intf $WinusbHandle -Register 0x06 -Value 0x0A # DAC de referencia analogica
    Write-SensorRegister -Intf $WinusbHandle -Register 0x09 -Value 0x0E # Ganancia global del amplificador
    Write-SensorRegister -Intf $WinusbHandle -Register 0x0E -Value 0x1A # Tiempo de exposicion - bit bajo
    Write-SensorRegister -Intf $WinusbHandle -Register 0x0F -Value 0x00 # Tiempo de exposicion - bit alto
    Write-SensorRegister -Intf $WinusbHandle -Register 0x10 -Value 0x06 # Control flanco sincronismo vertical
    Write-SensorRegister -Intf $WinusbHandle -Register 0x11 -Value 0x01 # Modo escaneo progresivo (anti-flicker)
    Write-SensorRegister -Intf $WinusbHandle -Register 0x14 -Value 0x03 # Rango dinamico y compresion
    Write-SensorRegister -Intf $WinusbHandle -Register 0x15 -Value 0x01 # Activar salida Bayer (color digital)
    # CRITICO: Reg 0x13 = 0x01 activa fisicamente la exposicion del sensor
    # Con 0x02 el sensor queda en standby y solo emite ceros
    Write-SensorRegister -Intf $WinusbHandle -Register 0x13 -Value 0x01 # ENCENDER EXPOSICION

    Write-Host '[!] HARDWARE EMITIENDO FLUJO NATIVO.' -ForegroundColor Green
    # CONFIRMADO: 16 paquetes x 128 bytes = 2048 bytes por llamada (limite del dispositivo)
    $PacketSize = [uint32]128
    $NumPackets = [uint32]16
    $Buffer     = New-Object byte[] ($PacketSize * $NumPackets)
    $PipeId     = [byte]0x81

    $StreamPath = Join-Path $PSScriptRoot 'stream_crudo.raw'
    $FileStream = [System.IO.File]::Open($StreamPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    $TotalBytes = 0

    Write-Host '[ Ingesta activa en Endpoint 0x81 (Alineamiento: 128B x64). CTRL+C para detener ]' -ForegroundColor Yellow

    while ($true) {
        $KernelErr = 0
        $BytesCapturados = [GeniusFinal.WinUsbFinalEngine]::LeerStreamingIsoch($WinusbHandle, $PipeId, $Buffer, $PacketSize, $NumPackets, [ref]$KernelErr)

        if ($BytesCapturados -gt 0) {
            $FileStream.Write($Buffer, 0, $BytesCapturados)
            $TotalBytes += $BytesCapturados
            Write-Host ('[STREAMING] {0} B volcados | Total: {1} KB' -f $BytesCapturados, [math]::Round($TotalBytes / 1024, 1)) -ForegroundColor Gray
        } elseif ($BytesCapturados -lt 0) {
            if ($KernelErr -ne 997) {
                Write-Host "[-] Interrupcion de bus. Codigo Kernel: $KernelErr" -ForegroundColor Red
                break
            }
        }
        [System.Threading.Thread]::Sleep(1)
    }
}
catch {
    Write-Host "[-] Ejecucion finalizada: $_" -ForegroundColor Red
}
finally {
    if ($null -ne $FileStream) { $FileStream.Close(); $FileStream.Dispose() }
    if ($WinusbHandle -ne [IntPtr]::Zero) {
        Write-CoreReg -Intf $WinusbHandle -Reg 0x01 -Value 0x04
        $null = [GeniusFinal.WinUsbFinalEngine]::WinUsb_Free($WinusbHandle)
    }
    if ($hFile -ne [IntPtr]::Zero -and $hFile.ToInt64() -ne -1) { $null = [GeniusFinal.WinUsbFinalEngine]::CloseHandle($hFile) }
    Write-Host '[+] Canal isocrono cerrado. Memoria no administrada liberada.' -ForegroundColor Green
}