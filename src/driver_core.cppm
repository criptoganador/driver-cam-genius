export module driver.core;

import <vector>;
import <expected>;
import <cstdint>; // Asegúrate de tener estos imports

// 1. Exportamos el tipo de error para que sea visible en .cpp
export enum class driver_error {
    device_io_failed,
    connection_failed
};

// 2. Exportamos la clase
export class camera_controller {
private:
    void* winusb_handle; // Asumo que este es el nombre de tu miembro privado

public:
    // Declaración de la función
    static std::expected<std::vector<uint8_t>, driver_error> get_configuration_descriptor();
    
    // ... otros métodos ...
};