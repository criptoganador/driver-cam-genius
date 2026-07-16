#include "bandwidth_manager.hpp"
#include <iostream>

namespace BandwidthManager {

    bool ApplySetting(WINUSB_INTERFACE_HANDLE handle, UCHAR settingIndex) {
        std::cout << "[BANDWIDTH] Intentando aplicar Setting #" << (int)settingIndex << "..." << std::endl;
        
        if (!WinUsb_SetCurrentAlternateSetting(handle, settingIndex)) {
            DWORD err = GetLastError();
            std::cerr << "[ERROR] Fallo al seleccionar setting " << (int)settingIndex 
                      << ". Error WinAPI: " << err << std::endl;
            return false;
        }
        
        std::cout << "[SUCCESS] Setting #" << (int)settingIndex << " aplicado correctamente." << std::endl;
        return true;
    }

    void ReportStatus(UCHAR settingIndex, bool success) {
        if (success) {
            std::cout << ">>> Configuracion " << (int)settingIndex << " aceptada por el hardware." << std::endl;
        } else {
            std::cout << ">>> Configuracion " << (int)settingIndex << " RECHAZADA." << std::endl;
        }
    }

    bool AutoSelectBestBandwidth(WINUSB_INTERFACE_HANDLE handle) {
        std::cout << "\n[INFO] Buscando el mejor ancho de banda disponible..." << std::endl;

        // Intentamos de la marcha 8 (la más rápida) hacia la 1
        for (int i = 8; i >= 1; --i) {
            std::cout << "[DEBUG] Probando Alternate Setting " << i << "... ";
            
            if (WinUsb_SetCurrentAlternateSetting(handle, (UCHAR)i)) {
                std::cout << "EXITO! Usando marcha " << i << "." << std::endl;
                return true; // Nos quedamos con esta
            } else {
                std::cout << "Rechazado." << std::endl;
            }
        }

        std::cerr << "[ERROR] No se pudo configurar ningun ancho de banda. Esta el bus muy saturado?" << std::endl;
        return false;
    }
}
