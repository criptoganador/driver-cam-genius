#pragma once
#include <vector>
#include <expected>
#include <span>
#include <cstdint>

// Definimos nuestros propios estados de protocolo
enum class ProtocolState {
    WaitingForHeader,
    ReadingPayload,
    FrameComplete,
    Error
};

class ProtocolHandler {
private:
    ProtocolState current_state = ProtocolState::WaitingForHeader;
    std::vector<uint8_t> frame_buffer;

public:
    // Procesa un chunk de datos brutos desde el hardware
    // Devuelve un std::expected con el frame completo si se ha formado uno
    std::expected<std::vector<uint8_t>, bool> process_chunk(std::span<const uint8_t> data) {
        
        for (const auto& byte : data) {
            switch (current_state) {
                case ProtocolState::WaitingForHeader:
                    // Aquí buscaremos los "Magic Bytes" (ej. 0x55, 0xAA)
                    // if (byte == 0x55) current_state = ReadingPayload;
                    break;
                case ProtocolState::ReadingPayload:
                    // Aquí acumulamos datos en frame_buffer
                    break;
                // ... más estados
            }
        }
        
        return std::unexpected(false); // Aún no hay frame completo
    }
};