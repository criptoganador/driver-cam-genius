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

// Inicializar la Genius iLook 317 (Chip SN9C120 / SN9C105)
void InitializeGenius317(WINUSB_INTERFACE_HANDLE winusb_handle);
