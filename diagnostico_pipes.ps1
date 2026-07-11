# =======================================================================
# DIAGNÓSTICO: ENUMERACIÓN DE PIPES / ENDPOINTS DEL DISPOSITIVO
# =======================================================================
if (-not ([System.Management.Automation.PSTypeName]'PipeDiag.WinUsbPipeDiag').Type) {
    $Sig = @"
    using System;
    using System.Runtime.InteropServices;

    namespace PipeDiag {
        // Tipos de endpoint USB
        public enum USBD_PIPE_TYPE { UsbdPipeTypeControl=0, UsbdPipeTypeIsochronous=1, UsbdPipeTypeBulk=2, UsbdPipeTypeInterrupt=3 }

        [StructLayout(LayoutKind.Sequential)]
        public struct WINUSB_PIPE_INFORMATION {
            public USBD_PIPE_TYPE PipeType;
            public byte PipeId;
            public ushort MaximumPacketSize;
            public byte Interval;
        }

        public class WinUsbPipeDiag {
            [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Auto)]
            public static extern IntPtr CreateFile(string fn, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);

            [DllImport("winusb.dll", SetLastError=true)]
            public static extern bool WinUsb_Initialize(IntPtr dev, out IntPtr intf);

            [DllImport("winusb.dll", SetLastError=true)]
            public static extern bool WinUsb_SetCurrentAlternateSetting(IntPtr intf, byte setting);

            [DllImport("winusb.dll", SetLastError=true)]
            public static extern bool WinUsb_QueryPipe(IntPtr intf, byte altSetting, byte idx, out WINUSB_PIPE_INFORMATION info);

            [DllImport("winusb.dll", SetLastError=true)]
            public static extern bool WinUsb_SetPipePolicy(IntPtr intf, byte pipeId, uint policyType, uint valueLength, ref uint value);

            [DllImport("winusb.dll", SetLastError=true)]
            public static extern bool WinUsb_ReadPipe(IntPtr intf, byte pipeId, byte[] buf, uint len, out uint read, IntPtr ovr);

            [DllImport("winusb.dll", SetLastError=true)]
            public static extern bool WinUsb_Free(IntPtr intf);

            [DllImport("kernel32.dll", SetLastError=true)]
            public static extern bool CloseHandle(IntPtr h);
        }
    }
"@
    Add-Type -TypeDefinition $Sig
}

$PnpDevice = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0C45&PID_60B0&MI_00*' } | Select-Object -First 1
if (-not $PnpDevice) { Write-Host '[-] Cámara no detectada.' -ForegroundColor Red; exit }
$CameraPath = '\\?\' + $PnpDevice.InstanceId.Replace('\', '#') + '#{dee824ef-729b-4a0e-9c14-b7117d33a817}'

$hFile = [PipeDiag.WinUsbPipeDiag]::CreateFile($CameraPath, [Convert]::ToUInt32('C0000000',16), 3, [IntPtr]::Zero, 3, [Convert]::ToUInt32('40000000',16), [IntPtr]::Zero)
if ($hFile -eq [IntPtr]::Zero -or $hFile.ToInt64() -eq -1) { Write-Host "[-] No se pudo abrir el dispositivo." -ForegroundColor Red; exit }

$Intf = [IntPtr]::Zero
if (-not [PipeDiag.WinUsbPipeDiag]::WinUsb_Initialize($hFile, [ref]$Intf)) { Write-Host "[-] WinUsb_Initialize falló." -ForegroundColor Red; [PipeDiag.WinUsbPipeDiag]::CloseHandle($hFile); exit }

# Cambiar a Alternate Setting 1 para exponer los endpoints de video
$null = [PipeDiag.WinUsbPipeDiag]::WinUsb_SetCurrentAlternateSetting($Intf, 1)

Write-Host "`n=== PIPES DISPONIBLES EN ALTERNATE SETTING 1 ===" -ForegroundColor Cyan
Write-Host ("{0,-8} {1,-12} {2,-18} {3}" -f "PipeId", "Tipo", "MaxPacketSize", "Intervalo") -ForegroundColor Yellow
Write-Host ("-" * 55) -ForegroundColor Gray

for ($i = 0; $i -lt 32; $i++) {
    $Info = New-Object PipeDiag.WINUSB_PIPE_INFORMATION
    if ([PipeDiag.WinUsbPipeDiag]::WinUsb_QueryPipe($Intf, 1, [byte]$i, [ref]$Info)) {
        $color = if ($Info.PipeType -eq [PipeDiag.USBD_PIPE_TYPE]::UsbdPipeTypeBulk) { 'Green' } else { 'Cyan' }
        Write-Host ("{0,-8} {1,-12} {2,-18} {3}" -f ("0x{0:X2}" -f $Info.PipeId), $Info.PipeType, $Info.MaximumPacketSize, $Info.Interval) -ForegroundColor $color
    } else {
        break  # No hay más pipes
    }
}

Write-Host "`n=== PRUEBA DIRECTA DE LECTURA EN EP DETECTADOS ===" -ForegroundColor Cyan

for ($i = 0; $i -lt 32; $i++) {
    $Info = New-Object PipeDiag.WINUSB_PIPE_INFORMATION
    if (-not [PipeDiag.WinUsbPipeDiag]::WinUsb_QueryPipe($Intf, 1, [byte]$i, [ref]$Info)) { break }
    if ($Info.PipeId -band 0x80) {  # Solo endpoints IN
        $mps = [uint32]$Info.MaximumPacketSize
        $bufSize = $mps * 64
        $buf = New-Object byte[] $bufSize
        [uint32]$read = 0
        [uint32]$timeout = 3000
        $null = [PipeDiag.WinUsbPipeDiag]::WinUsb_SetPipePolicy($Intf, $Info.PipeId, 0x03, 4, [ref]$timeout)
        $ok = [PipeDiag.WinUsbPipeDiag]::WinUsb_ReadPipe($Intf, $Info.PipeId, $buf, $bufSize, [ref]$read, [IntPtr]::Zero)
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($ok) {
            Write-Host ("[OK] EP 0x{0:X2} ({1}): Leídos {2} bytes" -f $Info.PipeId, $Info.PipeType, $read) -ForegroundColor Green
        } else {
            Write-Host ("[--] EP 0x{0:X2} ({1}): Error {2}" -f $Info.PipeId, $Info.PipeType, $err) -ForegroundColor Red
        }
    }
}

$null = [PipeDiag.WinUsbPipeDiag]::WinUsb_Free($Intf)
$null = [PipeDiag.WinUsbPipeDiag]::CloseHandle($hFile)
Write-Host "`n[+] Diagnóstico completo." -ForegroundColor Green
