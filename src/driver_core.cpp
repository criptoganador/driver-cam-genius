#include "driver_core.hpp"
#include <iostream>

std::expected<std::vector<uint8_t>, driver_error> camera_controller::get_configuration_descriptor() {
    
    hw_usb::USB_CONFIGURATION_DESCRIPTOR config_header{}; 
    ULONG bytes_returned = 0;

    // 1. Obtener encabezado para determinar el tamaño total necesario
    BOOL success = WinUsb_GetDescriptor(
        this->winusb_handle, 
        USB_CONFIGURATION_DESCRIPTOR_TYPE,
        0, 
        0, 
        reinterpret_cast<PUCHAR>(&config_header),
        sizeof(USB_CONFIGURATION_DESCRIPTOR),
        &bytes_returned
    );

    std::cout << "[DEBUG] WinUsb_GetDescriptor (Header) Status: " << (success ? "Success" : "Failed") << std::endl;
    
    if (!success || bytes_returned < sizeof(USB_CONFIGURATION_DESCRIPTOR)) {
        std::cerr << "[DEBUG] Error en lectura de encabezado: " << GetLastError() << std::endl;
        return std::unexpected(driver_error::descriptor_read_failed);
    }

    std::cout << "[DEBUG] wTotalLength reportado por hardware: " << config_header.wTotalLength << std::endl;

    // 2. Obtener descriptor completo usando el tamaño reportado
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
        std::cerr << "[DEBUG] Error en lectura de buffer completo: " << GetLastError() << std::endl;
        return std::unexpected(driver_error::device_io_failed);
    }

    // 3. INTEGRACIÓN: Alimentar al parser para procesar la estructura jerárquica
    this->parser.parse(full_buffer);
    std::cout << "[DEBUG] Descriptor parseado correctamente. Interfaces encontradas: " 
              << this->parser.interfaces.size() << std::endl;

    return full_buffer; 
}

    // 4. log devices info  filtro de la 9 interfaces 
    

void camera_controller::log_device_info() const {
    std::cout << "\n--- Desglose de Hardware (Interfaces Detectadas) ---\n";
    
    for (const auto& node : parser.interfaces) {
        const auto& iface = node.descriptor;
        
        // El Class 0x0E es el estándar UVC (USB Video Class)
        std::string type = (iface.bInterfaceClass == 0x0E) ? " [UVC Video]" : " [Other]";

        std::cout << "Interfaz #" << static_cast<int>(iface.bInterfaceNumber) 
                  << type 
                  << " | Class: 0x" << std::hex << (int)iface.bInterfaceClass 
                  << " | Subclass: 0x" << (int)iface.bInterfaceSubClass << std::dec << std::endl;

        for (const auto& ep : node.endpoints) {
            std::cout << "   -> Endpoint: 0x" << std::hex << (int)ep.bEndpointAddress 
                      << " | MaxPacketSize: " << std::dec << ep.wMaxPacketSize << std::endl;
        }
    }
    std::cout << "----------------------------------------------\n";
}

