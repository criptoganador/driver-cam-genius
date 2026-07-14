#include "driver_core.hpp"
#include <iostream>

// NOTA: Se eliminó 'static' de la definición. 
// Ahora es un método que pertenece a una instancia de camera_controller.
std::expected<std::vector<uint8_t>, driver_error> camera_controller::get_configuration_descriptor() {
    
    USB_CONFIGURATION_DESCRIPTOR config_header{}; 
    ULONG bytes_returned = 0;

    // Ahora winusb_handle es un miembro accesible de la clase (this-> es opcional pero válido)
    BOOL success = WinUsb_GetDescriptor(
        this->winusb_handle, 
        USB_CONFIGURATION_DESCRIPTOR_TYPE,
        0, 
        0, 
        reinterpret_cast<PUCHAR>(&config_header),
        sizeof(USB_CONFIGURATION_DESCRIPTOR),
        &bytes_returned
    );

    // Logs de depuración
    std::cout << "[DEBUG] WinUsb_GetDescriptor (Header) Status: " << (success ? "Success" : "Failed") << std::endl;
    std::cout << "[DEBUG] Bytes returned: " << bytes_returned << std::endl;
    
    if (success) {
        std::cout << "[DEBUG] wTotalLength reported: " << config_header.wTotalLength << std::endl;
    } else {
        std::cout << "[DEBUG] Error Code: " << GetLastError() << std::endl;
    }

    if (!success || bytes_returned < sizeof(USB_CONFIGURATION_DESCRIPTOR)) {
        return std::unexpected(driver_error::device_io_failed);
    }

    // Segundo paso: Obtener descriptor completo
    std::vector<uint8_t> full_buffer(config_header.wTotalLength);
    success = WinUsb_GetDescriptor(
        this->winusb_handle,
        USB_CONFIGURATION_DESCRIPTOR_TYPE,
        0,
        0,
        full_buffer.data(),
        static_cast<ULONG>(full_buffer.size()),
        &bytes_returned
    );

    if (!success || bytes_returned != config_header.wTotalLength) {
        return std::unexpected(driver_error::device_io_failed);
    }

    return full_buffer; 
}