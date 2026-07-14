#pragma once
#include <vector>
#include <expected>
#include <windows.h>
#include <winusb.h>
#include <cstdint>

enum class driver_error {
    device_io_failed,
    connection_failed
};

class camera_controller {
private:
    HANDLE winusb_handle; // El manejador ahora es parte del objeto

public:
    // Constructor: inicializa el controlador con un handle válido
    explicit camera_controller(HANDLE handle) : winusb_handle(handle) {}

    // MÉTODO DE INSTANCIA (sin 'static')
    std::expected<std::vector<uint8_t>, driver_error> get_configuration_descriptor();
};