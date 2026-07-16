#pragma once
#include <windows.h>
#include <winusb.h>

namespace BandwidthManager {
    // Retorna true si el cambio de setting fue exitoso
    bool ApplySetting(WINUSB_INTERFACE_HANDLE handle, UCHAR settingIndex);
    
    // Función de utilidad para ver si el cambio es soportado
    void ReportStatus(UCHAR settingIndex, bool success);

    // Intenta encontrar la marcha más alta posible empezando desde la 8 hacia abajo
    bool AutoSelectBestBandwidth(WINUSB_INTERFACE_HANDLE handle);
}
