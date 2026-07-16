#include <iostream>
#include <vector>
#include <windows.h>
#include <winusb.h>
#include "driver_core.hpp"
#include "genius_ilook_317.hpp"
#include "bandwidth_manager.hpp"
#include "video_capture.hpp"
#include "frame_sync.hpp"
#include "image_utils.hpp"
#include "display.hpp"

#include <setupapi.h>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>

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
        
        // 5. Auto-seleccionar el mejor ancho de banda (esto resetea el pipe internamente)
        if (!BandwidthManager::AutoSelectBestBandwidth(winusb_handle)) {
            std::cerr << "No se pudo establecer el ancho de banda. Abortando." << std::endl;
            WinUsb_Free(winusb_handle);
            CloseHandle(hDevice);
            return 1;
        }

        std::cout << "--- Dispositivo listo para transmitir video ---" << std::endl;

        // 6. Inicializar la camara con la secuencia completa (¡DESPUES de setear el ancho de banda!)
        if (!InitializeGenius317(winusb_handle)) {
            std::cerr << "Fallo en la inicializacion. Abortando." << std::endl;
            WinUsb_Free(winusb_handle);
            CloseHandle(hDevice);
            return 1;
        }

        // 7. Activar el DMA del SN9C103 (el "grifo" de datos al bus USB)
        //    InitializeGenius317 configura los registros del hardware,
        //    StartStreaming envía los bits exactos que activan el flujo de datos.
        if (!VideoCapture::StartStreaming(winusb_handle)) {
            std::cerr << "[WARN] StartStreaming fallo. Intentando leer de todos modos..." << std::endl;
        }

        // 8. Abrir ventana de visualización en tiempo real (SDL2)
        Display::Window display_win(320, 240, "Genius iLook 317 — Video en vivo");
        if (!display_win.IsAlive()) {
            std::cerr << "[WARN] No se pudo abrir la ventana SDL2. Solo se guardaran BMPs." << std::endl;
        }

        // 9. Iniciar el FrameSyncManager (hilo dedicado con Doble Buffer + Watchdog)
        std::cout << "\n--- Iniciando captura en tiempo real (ESC para salir) ---" << std::endl;

        std::atomic<int> total_frames{0};
        // Buffer compartido entre el hilo de captura y el hilo de render
        std::vector<uint8_t> shared_bgr(320 * 240 * 3, 0);
        std::atomic<bool> new_frame_ready{false};

        FrameSyncManager sync(winusb_handle, 0x81);
        sync.Start([&](const std::vector<uint8_t>& frame) {
            const size_t EXPECTED_RAW = 320 * 240; // 76,800 bytes

            // Filtrar frames claramente incompletos o corruptos
            if (frame.size() < 10000) {
                printf("[MAIN] Frame descartado: muy pequeño (%zu bytes)\n", frame.size());
                return;
            }

            // ── MODO RAW PURO (bit 7 de 0x18 = 0) ────────────────────────────
            // El SN9C103 envía exactamente 76,800 bytes de píxeles Bayer crudos.
            // Tomamos solo los primeros 76,800. Si vienen más, son basura de trama.
            std::vector<uint8_t> raw_frame;
            if (frame.size() >= EXPECTED_RAW) {
                raw_frame.assign(frame.begin(), frame.begin() + EXPECTED_RAW);
            } else {
                // Frame parcial: rellenar con gris neutro para no crashear
                raw_frame = frame;
                raw_frame.resize(EXPECTED_RAW, 128);
            }

            // ── PASO 2: Convertir RAW Bayer a BGR24 para SDL2 ─────────────────
            shared_bgr = BayerRGGBToBGR24(raw_frame, 320, 240);

            // Guardar el primer frame como BMP para análisis offline
            int frame_num = ++total_frames;
            if (frame_num == 3) { // Frame 3 = más probable tener exposición estabilizada
                SaveBMP("raw_debug.bmp", shared_bgr, 320, 240);
                printf("[MAIN] Frame RAW guardado en raw_debug.bmp (%zu bytes de entrada)\n",
                       frame.size());
            }

            new_frame_ready.store(true);

        });

        // ── BUCLE PRINCIPAL: render + eventos SDL en el hilo principal ────────
        while (display_win.ProcessEvents()) {
            if (new_frame_ready.exchange(false)) {
                display_win.UpdateFrame(shared_bgr, 320, 240);
            } else {
                // Sin frame nuevo: ceder CPU brevemente para no quemar el procesador
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        sync.Stop();
        printf("\n[MAIN] Sesion de video finalizada. Frames totales: %d\n",
               total_frames.load());
    }

    // Limpieza
    WinUsb_Free(winusb_handle);
    CloseHandle(hDevice);

    return 0;
}