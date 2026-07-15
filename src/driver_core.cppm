// ─── Global Module Fragment ────────────────────────────────────────────────────
// Los #include DEBEN ir aquí, ANTES de 'export module'.
// Esto hace que las cabeceras pertenezcan al módulo global (no a driver.core),
// evitando el error mismatched_owning_module.
module;
#include <vector>
#include <expected>
#include <cstdint>

// ─── Módulo nombrado ───────────────────────────────────────────────────────────
export module driver.core;

// 1. Exportamos el tipo de error
export enum class driver_error {
    device_io_failed,
    connection_failed
};

// 2. Exportamos la clase
export class camera_controller {
private:
    void* winusb_handle;

public:
    static std::expected<std::vector<uint8_t>, driver_error> get_configuration_descriptor();
};