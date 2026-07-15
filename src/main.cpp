#include <iostream>
#include <vector>
#include <windows.h>
#include <winusb.h>
#include "driver_core.hpp"
#include "genius_ilook_317.hpp"

#include <setupapi.h>
#include <string>

// GUID de la interfaz del dispositivo (Obtenido de la instalación de WinUSB)
// {dee30225-b74a-47bd-8e34-5c9c991fdf99}
const GUID WINUSB_INTERFACE_GUID = { 0xdee30225, 0xb74a, 0x47bd, { 0x8e, 0x34, 0x5c, 0x9c, 0x99, 0x1f, 0xdf, 0x99 } };

std::string GetDevicePath(const GUID& interfaceGuid) {
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(&interfaceGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return "";
    }

    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // Obtenemos el primer dispositivo que coincida con este GUID
    if (!SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &interfaceGuid, 0, &deviceInterfaceData)) {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return "";
    }

    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData, NULL, 0, &requiredSize, NULL);

    if (requiredSize == 0) {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return "";
    }

    std::vector<uint8_t> buffer(requiredSize);
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A deviceInterfaceDetailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_A>(buffer.data());
    deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

    if (!SetupDiGetDeviceInterfaceDetailA(deviceInfoSet, &deviceInterfaceData, deviceInterfaceDetailData, requiredSize, NULL, NULL)) {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return "";
    }

    std::string devicePath = deviceInterfaceDetailData->DevicePath;
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return devicePath;
}

int main() {
    std::cout << "--- Iniciando driver_genius: Conectando con hardware ---" << std::endl;

    std::string devicePath = GetDevicePath(WINUSB_INTERFACE_GUID);
    if (devicePath.empty()) {
        std::cerr << "Error: No se pudo encontrar el dispositivo. ¿Esta conectada la camara?" << std::endl;
        return 1;
    }

    std::cout << "Dispositivo detectado en puerto dinamico: \n" << devicePath << std::endl;

    // 1. Abrir el dispositivo
    HANDLE hDevice = CreateFileA(
        devicePath.c_str(),
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

        // Mostrar desglose de hardware
        cam.log_device_info();
        
        // 5. Inicializamos la Genius iLook 317
        InitializeGenius317(winusb_handle);
    }

    // Limpieza
    WinUsb_Free(winusb_handle);
    CloseHandle(hDevice);

    return 0;
}