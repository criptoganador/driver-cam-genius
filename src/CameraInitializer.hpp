#pragma once
#include <cstdint>
#include <vector>
#include <thread>
#include <chrono>

namespace CameraSystem {

    // Representa un comando I2C básico
    struct RegisterPair {
        uint8_t reg;
        uint8_t val;
        uint16_t delay_ms = 0; // Opcional: para estabilización
    };

    // Clase que maneja la secuencia sin conocer los detalles de implementación
    class SequenceExecutor {
    public:
        // Se inyecta la función de envío que ya tienes definida
        using SendFunc = void(*)(uint8_t, uint8_t);

        static void Run(const std::vector<RegisterPair>& sequence, SendFunc send_uvc_command) {
            for (const auto& [reg, val, delay] : sequence) {
                send_uvc_command(reg, val);
                if (delay > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                }
            }
        }
    };
}