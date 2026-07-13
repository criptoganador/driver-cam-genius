module;

#include <Windows.h> // Cambiado de windows.h a Windows.h
#include <expected>
#include <string_view>

export module driver.core;

import driver.types;
import std;

namespace genius::driver {

    export class camera_controller {
    public:
        static std::expected<camera_controller, driver_error> connect(std::string_view device_path) {
            HANDLE h_device = CreateFileA(
                device_path.data(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                nullptr
            );

            if (h_device == INVALID_HANDLE_VALUE) {
                return std::unexpected(driver_error::device_not_found);
            }

            return camera_controller{safe_handle(h_device)};
        }
    };
}