module;

// Global Module Fragment: headers legacy van aquí, antes de export module
#include <Windows.h>
#include <winusb.h>

export module driver.types;

// ─── Tipos de error del driver ────────────────────────────────────────────────
export enum class driver_error {
    device_not_found,
    device_io_failed,
    connection_failed,
    descriptor_read_failed
};

// ─── RAII wrapper para HANDLE de Windows ──────────────────────────────────────
export struct safe_handle {
    HANDLE value = INVALID_HANDLE_VALUE;

    explicit safe_handle(HANDLE h) : value(h) {}

    safe_handle(const safe_handle&) = delete;
    safe_handle& operator=(const safe_handle&) = delete;

    safe_handle(safe_handle&& other) noexcept : value(other.value) {
        other.value = INVALID_HANDLE_VALUE;
    }

    ~safe_handle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};