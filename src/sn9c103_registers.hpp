#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Registros del Puente Sonix SN9C103
//
// El SN9C103 actúa como "intermediario" entre el sensor OV7630 (que habla
// en señales analógicas HREF/VSYNC/PCLK) y el bus USB (que habla en paquetes).
//
// La función principal de estos registros es definir la "VENTANA DE HREF":
// el rectángulo de píxeles que el puente capturará de la señal del sensor.
// ─────────────────────────────────────────────────────────────────────────────

namespace SN9C103 {

    // ── Registros de Control Principal ───────────────────────────────────────
    constexpr uint16_t REG_CTRL        = 0x01; // Control: Clock, DMA, I2C enable
    constexpr uint16_t REG_CLOCK       = 0x01; // [Alias] Bit[2:1]=Clock, Bit[0]=DMA
    constexpr uint8_t  CTRL_CLK_24MHZ  = 0x44; // 24MHz al sensor
    constexpr uint8_t  CTRL_STREAM_ON  = 0x45; // 24MHz + DMA activo
    constexpr uint8_t  CTRL_STREAM_OFF = 0x44; // DMA apagado

    // ── Ganancias del Puente (Digital Gain antes de USB) ─────────────────────
    constexpr uint16_t REG_GAIN_R      = 0x05; // Ganancia Canal Rojo   (0x00-0x7F)
    constexpr uint16_t REG_GAIN_G      = 0x06; // Ganancia Canal Verde  (0x00-0x7F)
    constexpr uint16_t REG_GAIN_B      = 0x07; // Ganancia Canal Azul   (0x00-0x7F)
    constexpr uint8_t  GAIN_DEFAULT    = 0x40; // Ganancia 2x (Estándar)

    // ── Registros de I2C / SCCB Bridge ───────────────────────────────────────
    constexpr uint16_t REG_I2C_CTRL    = 0x08; // Status y control del bus I2C
    constexpr uint16_t REG_I2C_DATA    = 0x0D; // Dato leído en la última operación I2C

    // ── Ventana de Captura (HREF Window) ─────────────────────────────────────
    // Basado en sonixb.c:
    // 0x12 hstart
    // 0x13 vstart
    // 0x15 hsize (hsize = register-value * 16)
    // 0x16 vsize (vsize = register-value * 16)
    //
    constexpr uint16_t REG_H_START     = 0x12; // Horizontal start offset
    constexpr uint16_t REG_V_START     = 0x13; // Vertical start offset
    constexpr uint16_t REG_H_SIZE      = 0x15; // Horizontal size
    constexpr uint16_t REG_V_SIZE      = 0x16; // Vertical size

    // ── Registro de Formato / Compresión (0x18) ───────────────────────────────
    // Bit[7]  : 0 = RAW (sin comprimir), 1 = JPEG comprimido
    // Bit[5:4]: Scale — 00=1/1, 01=1/2, 10=1/4
    // Bit[3:0]: Otros controles de pipeline
    constexpr uint16_t REG_FORMAT      = 0x18;
    constexpr uint8_t  FMT_RAW_1_1    = 0x0F; // Sin comprimir, escala 1:1
    constexpr uint8_t  FMT_RAW_1_2    = 0x1F; // Sin comprimir, escala 1:2

    // ── Resoluciones Estándar — Valores para H_SIZE y V_SIZE ─────────────────
    //
    // Fórmula SN9C103 (sonixb.c):
    //   H_SIZE = (width  / 16)
    //   V_SIZE = (height / 16)
    //
    // Resolución 320x240 (QVGA — la ideal para USB Full-Speed sin compresión):
    constexpr uint8_t  QVGA_H_SIZE    = 0x14; // 320 / 16 = 20
    constexpr uint8_t  QVGA_V_SIZE    = 0x0F; // 240 / 16 = 15

    // Resolución 160x120 (QQVGA — para diagnóstico/bajo ancho de banda):
    constexpr uint8_t  QQVGA_H_SIZE   = 0x0A; // 160 / 16 = 10
    constexpr uint8_t  QQVGA_V_SIZE   = 0x07; // 112 / 16 = 7 (Aprox)

} // namespace SN9C103
