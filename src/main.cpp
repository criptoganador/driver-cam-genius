#include <iostream>
#include <vector>
#include <iomanip>
#include <windows.h>
#include <winusb.h>
#include "driver_core.hpp"

// NOTA: Reemplaza este string con el Device Path que obtuviste con tu mapper.ps1
// Ejemplo: "\\\\?\\usb#vid_xxxx&pid_xxxx#..."
const char* DEVICE_PATH = "\\\\?\\usb#vid_0c45&pid_60b0&mi_00#6&2056681f&0&0000#{dee30225-b74a-47bd-8e34-5c9c991fdf99}";

int main() {
    std::cout << "--- Iniciando driver_genius: Conectando con hardware ---" << std::endl;

    // 1. Abrir el dispositivo
    HANDLE hDevice = CreateFileA(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: No se pudo abrir el dispositivo. Codigo: " << GetLastError() << std::endl;
        return 1;
    }

    // 2. Inicializar WinUSB
    WINUSB_INTERFACE_HANDLE winusb_handle = INVALID_HANDLE_VALUE;
    if (!WinUsb_Initialize(hDevice, &winusb_handle)) {
        std::cerr << "Error: WinUsb_Initialize fallo. Codigo: " << GetLastError() << std::endl;
        CloseHandle(hDevice);
        return 1;
    }

    std::cout << "Conexión establecida correctamente." << std::endl;

    // 3. Instanciamos el controlador pasando el handle real
    camera_controller cam(winusb_handle); 

    // 4. Llamamos al método
    auto result = cam.get_configuration_descriptor();

    if (!result) {
        std::cerr << "Error: La camara no respondio correctamente." << std::endl;
    } else {
        std::cout << "Exito, datos recibidos." << std::endl;
        auto& buffer = *result;
        std::cout << "Tamano: " << buffer.size() << " bytes." << std::endl;
    }

    // Limpieza
    WinUsb_Free(winusb_handle);
    CloseHandle(hDevice);

    return 0;
}