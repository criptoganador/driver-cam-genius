#pragma once
#include <cstdint>

namespace OV7630 {
    // Direccion I2C (Slave Address) para escritura/lectura
    constexpr uint8_t SLAVE_ADDR_WRITE = 0x21; // En comandos del puente Sonix

    // Registros principales de configuracion del sensor OmniVision OV7630
    constexpr uint8_t REG_GAIN = 0x00; // Ganancia Global (AGC)
    constexpr uint8_t REG_BLUE = 0x01; // Ganancia Canal Azul (AWB)
    constexpr uint8_t REG_RED  = 0x02; // Ganancia Canal Rojo (AWB)
    
    constexpr uint8_t REG_PID  = 0x1C; // Product ID Number MSB (Debe ser 0x76)
    constexpr uint8_t REG_VER  = 0x1D; // Product ID Number LSB (Version)
    
    constexpr uint8_t REG_COM7 = 0x12; // Control Register 7 (Reset, Formato VGA/CIF)
    constexpr uint8_t COM7_RESET = 0x80; // Valor para reiniciar el sensor (SCCB Reset)

    constexpr uint8_t REG_BRT  = 0x24; // Control de Brillo (Brightness)
    
    // NOTA: Muchos otros registros (0x20-0x7D) corresponden a la matriz de calibracion
    // del fabricante para ajustes finos de la imagen (Gamma, Ruido, Blancos).
    // Suelen mantenerse como literales en la matriz de inicializacion base.
}
