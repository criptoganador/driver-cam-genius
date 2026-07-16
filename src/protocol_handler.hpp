#pragma once
#include <vector>
#include <optional>
#include <span>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// ProtocolHandler — Decodificador de paquetes SN9C103
//
// Responsabilidad única: Recibir chunks crudos de USB y ensamblar frames
// completos a partir del marcador SOF del puente Sonix.
//
// La firma SOF del SN9C103 es: FF FF 00 C4 C4 96 (6 bytes)
// Seguida de 12 bytes de cabecera interna = 18 bytes totales a descartar.
// ─────────────────────────────────────────────────────────────────────────────

enum class ProtocolState {
    WaitingForHeader,  // IDLE: Esperando el marcador VSYNC
    ReadingPayload,    // CAPTURING: Acumulando bytes del frame
    FrameComplete,     // TRANSFER_COMPLETE: Frame ensamblado y listo
    Error              // Error de protocolo
};

class ProtocolHandler {
public:
    // Firma SOF del puente SN9C103 (sonixb.c en el kernel de Linux)
    static constexpr uint8_t SOF_SIGNATURE[6] = {0xFF, 0xFF, 0x00, 0xC4, 0xC4, 0x96};
    static constexpr size_t  HEADER_SIZE      = 18; // Tamaño total de la cabecera SOF

    ProtocolHandler() { frame_buffer_.reserve(640 * 480); }

    // Reinicia el estado interno (útil al reconectar o resetear el sensor)
    void Reset() {
        current_state_ = ProtocolState::WaitingForHeader;
        frame_buffer_.clear();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // process_packet — Procesa UN paquete isócrono completo
    //
    // A diferencia del process_chunk original (que buscaba en medio de un stream),
    // este método opera correctamente a nivel de paquete, como lo hace el
    // driver sd_pkt_scan en sonixb.c del kernel de Linux.
    //
    // Retorna el frame completo si justo en este paquete se detectó un NUEVO
    // SOF mientras había un frame anterior acumulado.
    // ─────────────────────────────────────────────────────────────────────────
    std::optional<std::vector<uint8_t>> process_packet(std::span<const uint8_t> pkt) {
        if (pkt.empty()) return std::nullopt;

        std::optional<std::vector<uint8_t>> completed_frame;

        // Detectar si ESTE paquete comienza con el marcador SOF
        bool is_sof = (pkt.size() >= 6 &&
                       pkt[0] == SOF_SIGNATURE[0] && pkt[1] == SOF_SIGNATURE[1] &&
                       pkt[2] == SOF_SIGNATURE[2] && pkt[3] == SOF_SIGNATURE[3] &&
                       pkt[4] == SOF_SIGNATURE[4] && pkt[5] == SOF_SIGNATURE[5]);

        if (is_sof) {
            // Si ya teníamos un frame acumulado → emitirlo ahora
            if (current_state_ == ProtocolState::ReadingPayload &&
                frame_buffer_.size() > 256)
            {
                completed_frame = std::move(frame_buffer_);
                frame_buffer_.clear();
                frame_buffer_.reserve(640 * 480);
            }

            // Iniciar nuevo frame, descartando los 18 bytes de cabecera SOF
            current_state_ = ProtocolState::ReadingPayload;
            if (pkt.size() > HEADER_SIZE) {
                frame_buffer_.insert(frame_buffer_.end(),
                                     pkt.begin() + HEADER_SIZE, pkt.end());
            }

        } else if (current_state_ == ProtocolState::ReadingPayload) {
            // Paquete de continuación: agregar todos sus bytes al frame actual
            frame_buffer_.insert(frame_buffer_.end(), pkt.begin(), pkt.end());
        }
        // Si estamos en WaitingForHeader y no es SOF: ignorar (sin sincronización aún)

        return completed_frame;
    }

    // Acceso de solo lectura al estado actual (para diagnóstico)
    ProtocolState state() const { return current_state_; }

    // Bytes acumulados en el frame en construcción
    size_t bytes_accumulated() const { return frame_buffer_.size(); }

private:
    ProtocolState       current_state_ = ProtocolState::WaitingForHeader;
    std::vector<uint8_t> frame_buffer_;
};