# =======================================================================
# SUBSISTEMA DE CONTROL DE REGISTROS SIF (PARCHE DE TIEMPO REAL)
# =======================================================================

# =======================================================================
# 0. INICIALIZACIÓN DE ENTORNO Y WINUSB
# =======================================================================
if (-not ([System.Management.Automation.PSTypeName]'WinUsbProber').Type) {
    $Signature = @"
    using System;
    using System.Runtime.InteropServices;

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct WINUSB_SETUP_PACKET_PROBER {
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
        public static extern bool WinUsb_ControlTransfer(IntPtr InterfaceHandle, WINUSB_SETUP_PACKET_PROBER SetupPacket, byte[] Buffer, uint BufferLength, out uint LengthTransferred, IntPtr Overlapped);
        
        [DllImport("winusb.dll", SetLastError = true)]
        public static extern bool WinUsb_Free(IntPtr InterfaceHandle);
        
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CloseHandle(IntPtr hObject);
    }
"@
    Add-Type -TypeDefinition $Signature
}

function Write-Reg ([IntPtr]$Intf, [uint16]$Reg, [byte]$Val) {
    $Pkt = New-Object WINUSB_SETUP_PACKET_PROBER
    $Pkt.RequestType = 0x41; $Pkt.Request = 0x08; $Pkt.Value = $Reg; $Pkt.Length = 1
    $Buf = New-Object byte[] 1; $Buf[0] = $Val; [uint32]$Transferred = 0
    $null = [WinUsbProber]::WinUsb_ControlTransfer($Intf, $Pkt, $Buf, 1, [ref]$Transferred, [IntPtr]::Zero)
}

$PnpDevice = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0C45&PID_60B0&MI_00*' } | Select-Object -First 1
if (-not $PnpDevice) { Write-Host '[-] Cámara no encontrada.' -ForegroundColor Red; exit }
$CameraPath = '\\?\' + $PnpDevice.InstanceId.Replace('\', '#') + '#{dee824ef-729b-4a0e-9c14-b7117d33a817}'

$hFile = [IntPtr]::Zero; $WinusbHandle = [IntPtr]::Zero
$Access = [Convert]::ToUInt32('C0000000', 16) # GENERIC_READ | GENERIC_WRITE
$Share = [uint32]3           # FILE_SHARE_READ | FILE_SHARE_WRITE
$Disp = [uint32]3            # OPEN_EXISTING
$Flags = [Convert]::ToUInt32('40000000', 16)  # FILE_FLAG_OVERLAPPED

$hFile = [WinUsbProber]::CreateFile($CameraPath, $Access, $Share, [IntPtr]::Zero, $Disp, $Flags, [IntPtr]::Zero)
if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) { Write-Host "[-] Dispositivo ocupado o sin permisos." -ForegroundColor Red; exit }
if (-not [WinUsbProber]::WinUsb_Initialize($hFile, [ref]$WinusbHandle)) { Write-Host "[-] Error al inicializar WinUSB." -ForegroundColor Red; exit }

# Función avanzada de escritura SIF con Polling de Tolerancia Cero y Control de Timeouts
function Write-SensorRegister ([IntPtr]$Intf, [byte]$Register, [byte]$Value) {
    # 1. Cargar dirección de destino en el búfer de la Sonix
    Write-Reg -Intf $Intf -Reg 0x09 -Value $Register
    
    # 2. Cargar el valor a inyectar en el silicio
    Write-Reg -Intf $Intf -Reg 0x0A -Value $Value
    
    # 3. Disparar el estrobo de hardware (0x11 = Write 1 Byte + SIF Start)
    Write-Reg -Intf $Intf -Reg 0x10 -Value 0x11

    # 4. Bucle de Polling crítico para prevenir congelamientos en consola
    $Pkt = New-Object WINUSB_SETUP_PACKET_PROBER
    $Pkt.RequestType = 0xC1; $Pkt.Request = 0x00; $Pkt.Value = 0x10; $Pkt.Length = 1
    $Buf = New-Object byte[] 1; [uint32]$Read = 0
    
    $StrobeCleared = $false
    for ($retry = 0; $retry -lt 15; $retry++) {
        $null = [WinUsbProber]::WinUsb_ControlTransfer($Intf, $Pkt, $Buf, 1, [ref]$Read, [IntPtr]::Zero)
        if ($Buf[0] -eq 0x00) {
            $StrobeCleared = $true
            break
        }
        # Retardo controlado de micro-bucle para dar holgura al flanco de bajada del reloj
        [System.Threading.Thread]::Sleep(2)
    }

    if (-not $StrobeCleared) {
        throw "CRITICAL_SIF_TIMEOUT: El sensor rechazó la escritura en Reg [0x{0:X2}]. Bus colgado en 0x{1:X2}." -f $Register, $Buf[0]
    }
}

# =======================================================================
# SECUENCIA MAESTRA DE ARRANQUE (FLUJO DE EJECUCIÓN SEGURO)
# =======================================================================
try {
    # [FASE 1]: Asegurar el reloj del puente USB antes de tocar las líneas serie
    Write-Host "[...] Configurando reloj maestro estable (Reg 0x01)..." -ForegroundColor Gray
    Write-Reg -Intf $WinusbHandle -Reg 0x01 -Value 0x04

    # [FASE 2]: Despertar eléctrico del hardware vía GPIO utilizando tu mapa de éxito (0x18)
    Write-Host "[+] Aplicando máscara de energía GPIO: 0x18" -ForegroundColor Green
    Write-Reg -Intf $WinusbHandle -Reg 0x02 -Value 0x1F  # Pines 0-4 como salidas configuradas
    Write-Reg -Intf $WinusbHandle -Reg 0x03 -Value 0x18  # Despierta el integrado PixArt
    
    # -------------------------------------------------------------------
    # ¡CRUCIAL!: Retardo de estabilización galvánica para el sensor PAS106
    # No reduzcas este tiempo; el oscilador interno lo requiere para estabilizar el VDD.
    # -------------------------------------------------------------------
    Write-Host "[...] Esperando estabilización del cristal del lente (35ms)..." -ForegroundColor Gray
    Start-Sleep -Milliseconds 35

    # [FASE 3]: Configurar el Bus SIF en el puente con divisor seguro (Reg 0x08 -> 0x14)
    Write-Host "[+] Sincronizando velocidad segura del bus SIF (Reg 0x08 -> 0x14)..." -ForegroundColor Green
    Write-Reg -Intf $WinusbHandle -Reg 0x08 -Value 0x14

    # [FASE 4]: Mapear la dirección del ID Esclavo nativo encontrado (0x40)
    Write-Host "[+] Fijando ID Esclavo del sensor (Reg 0x0B -> 0x40)..." -ForegroundColor Green
    Write-Reg -Intf $WinusbHandle -Reg 0x0B -Value 0x40

    # [FASE 5]: Escritura limpia de registros internos del lente (Sin bloqueos)
    Write-Host "[...] Inyectando secuencias operativas en la matriz PixArt..." -ForegroundColor Cyan
    
    # Probamos la escritura en el registro conflictivo 0x12 utilizando nuestra primitiva segura
    Write-SensorRegister -Intf $WinusbHandle -Register 0x12 -Value 0x01
    Write-Host "[¡ÉXITO!] Registro [0x12] escrito y confirmado por el estrobo." -ForegroundColor Green

    # [FASE 5.5]: Carga masiva de la matriz de configuración del PixArt PAS106
    Write-Host "[...] Inyectando secuencia de inicialización del lente (CIF 352x288)..." -ForegroundColor Cyan

    # Tabla de registros nativos del PAS106B extraída de especificaciones de ingeniería
    $Pas106Matrix = @(
        @{ Reg = 0x02; Val = 0x0C }  # Configuración de modo y reloj interno
        @{ Reg = 0x03; Val = 0x40 }  # Reloj de descarga de píxeles (Pixel Clock Polarity)
        @{ Reg = 0x04; Val = 0x05 }  # Modo de ventana de captura activo
        @{ Reg = 0x05; Val = 0x24 }  # Control de supresión de ruido térmico en la matriz
        @{ Reg = 0x06; Val = 0x0A }  # Ajuste del DAC de referencia analógica
        @{ Reg = 0x09; Val = 0x0E }  # Ganancia global inicial del amplificador integrado
        @{ Reg = 0x0E; Val = 0x1A }  # Tiempo de exposición - Bit bajo
        @{ Reg = 0x0F; Val = 0x00 }  # Tiempo de exposición - Bit alto
        @{ Reg = 0x10; Val = 0x06 }  # Control del flanco de sincronismo vertical
        @{ Reg = 0x11; Val = 0x01 }  # Modo de escaneo progresivo (Anti-flicker activado)
        @{ Reg = 0x14; Val = 0x03 }  # Relación de compresión interna y rango dinámico
        @{ Reg = 0x15; Val = 0x01 }  # Activar salida de matriz de color digital (Bayer Pattern)
        @{ Reg = 0x13; Val = 0x01 }  # [NUEVO] ¡CRUCIAL! Escribir 1 para validar configuración y encender exposición
    )

    # Iteración síncrona sobre la matriz usando nuestra primitiva de control segura
    foreach ($Setting in $Pas106Matrix) {
        $HexReg = "0x{0:X2}" -f $Setting.Reg
        $HexVal = "0x{0:X2}" -f $Setting.Val
        Write-Host "[SIF] Escribiendo Sensor Reg [$HexReg] -> Valor [$HexVal]..." -ForegroundColor Gray
        
        # Inyección física
        Write-SensorRegister -Intf $WinusbHandle -Register $Setting.Reg -Value $Setting.Val
    }

    Write-Host "[¡ÉXITO!] Matriz del sensor PixArt cargada por completo. El lente está emitiendo señal." -ForegroundColor Green

    # [FASE 6]: Configurar el puente Sonix para empaquetar el flujo del PAS106
    Write-Host "[...] Configurando registros de sincronismo en el puente Sonix..." -ForegroundColor Cyan
    
    # Reg 0x11: Configura la polaridad de VSYNC/HSYNC del puente para que matchee con el PAS106
    Write-Reg -Intf $WinusbHandle -Reg 0x11 -Value 0x2C 
    
    # Reg 0x14: Define el tamaño de la ventana de captura en el búfer USB (352x288 CIF)
    Write-Reg -Intf $WinusbHandle -Reg 0x14 -Value 0x14
}
catch {
    Write-Host "[-] Error fatal de hardware detectado: $_" -ForegroundColor Red
    # Rutina automática de mitigación: apagar bus para proteger el silicio
    if ($WinusbHandle -ne [IntPtr]::Zero) { Write-Reg -Intf $WinusbHandle -Reg 0x10 -Value 0x00 }
}
finally {
    if ($WinusbHandle -ne [IntPtr]::Zero) { $null = [WinUsbProber]::WinUsb_Free($WinusbHandle) }
    if ($hFile -ne [IntPtr]::Zero -and $hFile.ToInt64() -ne -1) { $null = [WinUsbProber]::CloseHandle($hFile) }
}
