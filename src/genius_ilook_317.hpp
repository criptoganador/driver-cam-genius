#pragma once
#include <vector>
#include <cstdint>
#include <windows.h>
#include <winusb.h>

// Estructura para comandos de inicialización
struct InitCommand {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    std::vector<uint8_t> data;
};

// Helper para enviar comandos de control a la cámara
bool send_control_transfer(WINUSB_INTERFACE_HANDLE handle, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, std::vector<uint8_t>& data);

// Función de validación modular (Sanity Check)
bool VerifySensorIdentity(WINUSB_INTERFACE_HANDLE handle);

// Inicializar la Genius iLook 317 (Chip SN9C120 / SN9C105)
bool InitializeGenius317(WINUSB_INTERFACE_HANDLE winusb_handle);

// --- Ajustes Dinámicos en Tiempo Real ---
// Estas funciones permiten modificar los valores del sensor mientras ya está capturando.
bool SetSensorGain(WINUSB_INTERFACE_HANDLE handle, uint8_t gain_level);
bool SetSensorBrightness(WINUSB_INTERFACE_HANDLE handle, uint8_t brightness_level);
