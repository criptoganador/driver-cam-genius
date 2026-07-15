#pragma once
#include <vector>
#include <expected>
#include <cstdint>
#include <windows.h>
#include <winusb.h>
#include "usb_descriptors.hpp" // Dependencia del parser

enum class driver_error {
    device_io_failed,
    connection_failed,
    descriptor_read_failed
};

class camera_controller {
private:
    WINUSB_INTERFACE_HANDLE winusb_handle; // Handle específico de WinUSB
    DescriptorParser parser;              // Instancia del parser integrada

public:
    // Constructor
    explicit camera_controller(WINUSB_INTERFACE_HANDLE handle) : winusb_handle(handle) {}
    // Añade este método en la sección 'public' de tu clase camera_controller
    void log_device_info() const;
    // Método para obtener y parsear el descriptor automáticamente
    std::expected<std::vector<uint8_t>, driver_error> get_configuration_descriptor();

    // Getter para acceder a la jerarquía de hardware procesada
    const DescriptorParser& get_parser() const { return parser; }
};