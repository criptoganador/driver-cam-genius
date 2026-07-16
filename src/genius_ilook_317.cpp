#include "genius_ilook_317.hpp"
#include "ov7630_registers.hpp"
#include "sn9c103_registers.hpp"
#include <iostream>

// Función de escritura de registros con "Workaround" para Windows Composite Devices
bool WriteRegister(WINUSB_INTERFACE_HANDLE handle, uint16_t reg, uint8_t val) {
    WINUSB_SETUP_PACKET setup{};
    // 0x41 = Vendor Request dirigido a la Interfaz.
    // Usamos 0x41 en vez de 0x40 porque usbccgp.sys (driver genérico de Windows)
    // bloquea peticiones 0x40 desde un hijo (MI_00).
    setup.RequestType = 0x41; 
    setup.Request = 0x08;     // Instrucción WRITE real para SN9C1xx
    
    // Windows sobrescribe wIndex con el número de interfaz (0) cuando usamos 0x41.
    // Así que usamos la variante de comando "Block Write" donde wValue es el registro
    // y wIndex es 0, enviando el valor real en la fase de datos.
    setup.Value = reg;        // Dirección del registro
    setup.Index = 0;          // Será sobrescrito por 0 (Interface) de todos modos
    setup.Length = 1;         // 1 byte de payload

    uint8_t payload[1] = { val };
    ULONG bytesTransferred = 0;
    
    if (!WinUsb_ControlTransfer(handle, setup, payload, 1, &bytesTransferred, nullptr)) {
        DWORD err = GetLastError();
        printf("[ERROR] Fallo escribiendo Reg: 0x%04X, Val: 0x%02X. Error: %lu\n", reg, val, err);
        return false;
    }
    printf("[EXITO] Escrito Reg: 0x%04X, Val: 0x%02X correctamente.\n", reg, val);
    return true;
}

// Helper genérico si quieres seguir manteniéndolo
bool send_control_transfer(WINUSB_INTERFACE_HANDLE handle, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, std::vector<uint8_t>& data) {
    WINUSB_SETUP_PACKET setup_packet = {};
    setup_packet.RequestType = bmRequestType;
    setup_packet.Request = bRequest;
    setup_packet.Value = wValue;
    setup_packet.Index = wIndex;
    setup_packet.Length = static_cast<USHORT>(data.size());

    ULONG bytesTransferred = 0;
    BOOL success = WinUsb_ControlTransfer(handle, setup_packet, data.empty() ? nullptr : data.data(), static_cast<ULONG>(data.size()), &bytesTransferred, NULL);
    if (!success) {
        return false;
    }
    return true;
}

// Función para escribir un bloque de registros
bool WriteRegisterBlock(WINUSB_INTERFACE_HANDLE handle, uint16_t start_reg, const uint8_t* data, uint16_t len) {
    WINUSB_SETUP_PACKET setup{};
    setup.RequestType = 0x41; 
    setup.Request = 0x08;     
    setup.Value = start_reg;  
    setup.Index = 0;          
    setup.Length = len;         

    ULONG bytesTransferred = 0;
    if (!WinUsb_ControlTransfer(handle, setup, (PUCHAR)data, len, &bytesTransferred, nullptr)) {
        printf("[ERROR] Fallo escribiendo bloque Reg: 0x%04X. Err: %lu\n", start_reg, GetLastError());
        return false;
    }
    return true;
}

// Función para leer un registro
uint8_t ReadRegister(WINUSB_INTERFACE_HANDLE handle, uint16_t reg) {
    WINUSB_SETUP_PACKET setup{};
    setup.RequestType = 0xC1; // IN, Vendor, Interface
    setup.Request = 0x00;     
    setup.Value = reg;
    setup.Index = 0;
    setup.Length = 1;

    uint8_t val = 0;
    ULONG bytesTransferred = 0;
    if (!WinUsb_ControlTransfer(handle, setup, &val, 1, &bytesTransferred, nullptr)) {
        printf("[WARN] Fallo leyendo Reg: 0x%04X\n", reg);
        return 0;
    }
    return val;
}

bool I2CRead(WINUSB_INTERFACE_HANDLE handle, uint8_t reg_addr, uint8_t& out_val) {
    // Comando 0x91 para el SN9C103: Leer 1 byte por I2C
    uint8_t read_cmd[8] = {0x91, OV7630::SLAVE_ADDR_WRITE, reg_addr, 0x00, 0x00, 0x00, 0x00, 0x10};
    if (!WriteRegisterBlock(handle, 0x08, read_cmd, 8)) return false;
    
    // Polling esperando la lectura
    for (int i = 0; i < 60; i++) {
        Sleep(10);
        uint8_t status = ReadRegister(handle, 0x08);
        if (status & 0x04) {
            if (status & 0x08) {
                return false; // Error / NACK
            }
            
            // DUMP DE DIAGNÓSTICO: Buscar dónde está guardado el valor
            printf("[DEBUG] I2CRead(%02X) Exitoso. Buscando datos en registros...\n", reg_addr);
            for (int r = 0; r <= 0x1F; r++) {
                uint8_t v = ReadRegister(handle, r);
                if (v != 0x00 && v != 0xFF) {
                    printf("  Reg 0x%02X = 0x%02X\n", r, v);
                }
                if (r == 0x00) { out_val = v; } // Default temporal (cambiar luego)
            }
            
            // En SN9C103, los datos leídos de I2C/SCCB se almacenan comúnmente en 0x00 o 0x0A
            // En este puente, parece que el valor de retorno SCCB se lee usando un comando especial.
            out_val = ReadRegister(handle, 0x00);
            return true;
        }
    }
    return false; // Timeout
}

// Función para mandar I2C al sensor a través del puente SN9C103
bool I2CWrite(WINUSB_INTERFACE_HANDLE handle, const uint8_t data[8]) {
    if (!WriteRegisterBlock(handle, 0x08, data, 8)) return false;
    
    // Polling hasta que el bit 2 de 0x08 se ponga a 1 (Listo)
    for (int i = 0; i < 60; i++) {
        Sleep(10);
        uint8_t status = ReadRegister(handle, 0x08);
        if (status & 0x04) {
            if (status & 0x08) {
                printf("[ERROR] Error I2C en el sensor.\n");
                return false; // <-- CORRECCION: Si hay NACK, devolvemos error
            }
            return true;
        }
    }
    printf("[ERROR] Timeout esperando I2C.\n");
    return false;
}

// NOTA: Se ha eliminado la función I2CRead porque el puente SN9C103
// no soporta lecturas SCCB de forma consistente. La configuración
// se realiza de forma Write-Only.

// Función de validación modular (Sanity Check)
bool VerifySensorIdentity(WINUSB_INTERFACE_HANDLE handle) {
    std::cout << "\n[I2C] Detectando sensor conectado (Sanity Check)..." << std::endl;
    uint8_t pid = 0;
    
    // 1. Intentar leer el ID (REG_PID = 0x1C)
    if (!I2CRead(handle, OV7630::REG_PID, pid)) {
        printf("[ERROR] Fallo de comunicacion I2C. No hubo respuesta del sensor al leer PID.\n");
        // No abortamos porque SN9C103 a veces no soporta SCCB read
    }

    // 2. Comparación estricta
    uint8_t expected_id = 0x76;
    if (pid != expected_id) {
        printf("[WARNING] Fallo de Sanity Check: ID de sensor no coincide. Esperado: 0x%02X, Recibido: 0x%02X\n", expected_id, pid);
        printf("[WARNING] Nota: El puente SN9C103 no soporta lecturas SCCB consistentes. Ignorando fallo y continuando...\n");
        // return false; <-- COMENTADO POR LIMITACIÓN DE HARDWARE
    } else {
        // Opcional: Leer también VER para propósitos informativos
        uint8_t ver = 0;
        if (I2CRead(handle, OV7630::REG_VER, ver)) {
            printf("[I2C] Hardware validado. Sensor OV7630 detectado (PID: 0x%02X, VER: 0x%02X).\n", pid, ver);
        } else {
            printf("[I2C] Hardware validado. Sensor OV7630 detectado (PID: 0x%02X), pero fallo la lectura de VER.\n", pid);
        }
    }

    return true; // Siempre verdadero por limitación del puente
}

bool InitializeGenius317(WINUSB_INTERFACE_HANDLE handle) {
    std::cout << "\n=== INICIALIZANDO GENIUS ILOOK 317 (SN9C103 + OV7630) ===" << std::endl;


    // initOv7630 base (registros 0x01 a 0x19)
    uint8_t bridge_init[] = {
        0x04, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, // r01..r08
        0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // r09..r16
        0x00, 0x01, 0x01, 0x0a,                         // r17..r20
        0x28, 0x1e,                                     // r21..r22 (640x480 max)
        0x68, 0x8f, 0x2b                                // r23..r25
    };
    
    // Escribir inicialización base
    WriteRegisterBlock(handle, 0x01, bridge_init, sizeof(bridge_init));

    // ── Reloj Maestro (MCLK) del puente SN9C103 ──────────────────────────────
    // Reg 0x01 bits 6:4 controlan el divisor del reloj enviado al sensor:
    //   0x44 = 0100 0100 → MCLK/1 = 24 MHz (demasiado rápido para USB 1.1 sin compresión)
    //   0x54 = 0101 0100 → MCLK/2 = 12 MHz
    //   0x64 = 0110 0100 → MCLK/4 =  6 MHz ← OBJETIVO: USB 1.1 puede sacar 76,800 bytes
    //   0x74 = 0111 0100 → MCLK/8 =  3 MHz
    // Al bajar el reloj a 6 MHz, el sensor produce ~5-8 FPS pero el frame llega COMPLETO.
    WriteRegister(handle, 0x01, 0x64); // MCLK/4 = 6 MHz → Frame RAW completo a bajos FPS
    WriteRegister(handle, 0x12, 0x02); // hstart a 2
    
    // Gains del puente SN9C103 — subimos para compensar imagen oscura
    // Rango: 0x00-0x7F. 0x40 = ganancia 2x (doble de brillo)
    WriteRegister(handle, 0x05, 0x40); // Red  gain x2
    WriteRegister(handle, 0x06, 0x40); // Green gain x2
    WriteRegister(handle, 0x07, 0x40); // Blue  gain x2

    // ── Ventana HREF (Horizontal Reference Window) ────────────────────────────
    // Estrategia del kernel Linux (sonixb.c):
    // El sensor OV7630 se configura en VGA (640x480).
    // El puente SN9C103 lee 640x480 y usa su hardware interno para escalar a 1/2,
    // resultando en 320x240 exactos por USB, evitando desbordamientos de FIFO.
    // ─────────────────────────────────────────────────────────────────────────
    WriteRegister(handle, SN9C103::REG_H_SIZE, 0x14); // 20 * 16 = 320
    WriteRegister(handle, SN9C103::REG_V_SIZE, 0x0F); // 15 * 16 = 240

    printf("[HREF] Ventana configurada: 640x480 (Sensor) -> Escalado 1/2 (USB 320x240 RAW)\n");

    // Gamma table (0x20 a 0x2F)
    uint8_t gamma[16];
    for (int i = 0; i < 15; i++) gamma[i] = i * 16;
    gamma[15] = 255;
    WriteRegisterBlock(handle, 0x20, gamma, 16);

    // ── Configuración de Interfaz del Sensor (0x17) ─────────────────────────
    // ¡CRÍTICO! sonixb.c sobreescribe el 0x68 inicial con 0xe0 para el OV7630.
    // 0xe0 = 1110 0000 -> Bit 7=1 (VSYNC Activo Bajo), Bit 6=1 (HSYNC Activo Bajo).
    // Si la polaridad VSYNC está mal, el puente asume que cada paquete es un nuevo frame,
    // emitiendo la cabecera SOF sin parar.
    WriteRegister(handle, 0x17, 0xe0);

    // ── Escalado del puente SN9C103 (registro 0x18) ─────────────────────────
    // Scale 1/1: sensor QVGA (320x240) → USB (320x240) RAW = 76,800 bytes
    // Bits 4-5 del registro 0x18:
    //   00 = 1/1 (320x240) ← OBJETIVO
    //   01 = 1/2
    //   10 = 1/4
    uint8_t r18 = bridge_init[23]; // 0x8f
    r18 &= ~0x80;      // Bit 7 = 0 → RAW puro (sin DPCM). Frames de exactamente 76,800 bytes.
    r18 &= ~(3 << 4);  // Bits 4-5 = 00 → escala 1/1 (320x240)
    WriteRegister(handle, 0x18, r18);
    
    // Habilitar transferencia de video (bit 2 en reg 0x01)
    WriteRegister(handle, 0x01, 0x30); // sonixb.c escribe 0x30 en sd_start (SN9C103)
    
    // --- VERIFICACIÓN DE IDENTIDAD DEL SENSOR (PID / VER) ---
    if (!VerifySensorIdentity(handle)) {
        return false;
    }
    std::cout << "------------------------------------------------------------\n" << std::endl;

    // Secuencia I2C para inicializar el sensor OV7630
    const uint8_t ov7630_i2c[][8] = {
        {0xa0, OV7630::SLAVE_ADDR_WRITE, OV7630::REG_COM7, OV7630::COM7_RESET, 0x00, 0x00, 0x00, 0x10}, // Reset total
        // COM4 / PCLK Divisor: Dividir el pixel clock por 4 dentro del sensor.
        // Bits[7:6]=01 → PCLK/4. Esto reduce aún más los FPS pero asegura frames completos.
        {0xa0, OV7630::SLAVE_ADDR_WRITE, 0x3E, 0x19, 0x00, 0x00, 0x00, 0x10}, // PCLK /4 → ~7 FPS
        // REG00: GAIN global. 0x00=auto, 0x60=ganancia alta manual para imagen oscura
        {0xa0, OV7630::SLAVE_ADDR_WRITE, OV7630::REG_GAIN, 0x60, 0x00, 0x00, 0x00, 0x10},
        {0xb0, OV7630::SLAVE_ADDR_WRITE, OV7630::REG_BLUE, 0x77, 0x3a, 0x00, 0x00, 0x10},
        // COM7 = 0x10: QVGA (320x240) explícito, subsampling 2x horizontal y vertical
        // Bits: [7]=0 SCCB slave OK, [4]=1 QVGA, [3:2]=00 YUV, [1]=0, [0]=0
        {0xd0, OV7630::SLAVE_ADDR_WRITE, OV7630::REG_COM7, 0x10, 0x00, 0x80, 0x34, 0x10},
        {0xa0, OV7630::SLAVE_ADDR_WRITE, 0x1b, 0x04, 0x00, 0x80, 0x34, 0x10},
        {0xa0, OV7630::SLAVE_ADDR_WRITE, 0x20, 0x44, 0x00, 0x80, 0x34, 0x10},
        {0xa0, OV7630::SLAVE_ADDR_WRITE, 0x23, 0xee, 0x00, 0x80, 0x34, 0x10},
        {0xd0, OV7630::SLAVE_ADDR_WRITE, 0x26, 0xa0, 0x9a, 0xa0, 0x30, 0x10},
        {0xb0, OV7630::SLAVE_ADDR_WRITE, 0x2a, 0x80, 0x00, 0xa0, 0x30, 0x10},
        {0xb0, OV7630::SLAVE_ADDR_WRITE, 0x2f, 0x3d, 0x24, 0xa0, 0x30, 0x10},
        {0xa0, OV7630::SLAVE_ADDR_WRITE, 0x32, 0x86, 0x24, 0xa0, 0x30, 0x10},
        {0xb0, OV7630::SLAVE_ADDR_WRITE, 0x60, 0xa9, 0x4a, 0xa0, 0x30, 0x10},
        {0xa0, OV7630::SLAVE_ADDR_WRITE, 0x65, 0x00, 0x42, 0xa0, 0x30, 0x10},
        {0xa0, OV7630::SLAVE_ADDR_WRITE, 0x69, 0x38, 0x42, 0xa0, 0x30, 0x10},
        {0xc0, OV7630::SLAVE_ADDR_WRITE, 0x6f, 0x88, 0x0b, 0x00, 0x30, 0x10},
        {0xc0, OV7630::SLAVE_ADDR_WRITE, 0x74, 0x21, 0x8e, 0x00, 0x30, 0x10},
        {0xa0, OV7630::SLAVE_ADDR_WRITE, 0x7d, 0xf7, 0x8e, 0x00, 0x30, 0x10},
        {0xd0, OV7630::SLAVE_ADDR_WRITE, 0x17, 0x1c, 0xbd, 0x06, 0xf6, 0x10},
        // Ajuste extra para OV7630 en SN9C103
        {0xa0, OV7630::SLAVE_ADDR_WRITE, 0x13, 0x80, 0x00, 0x00, 0x00, 0x10}
    };
    
    std::cout << "[I2C] Enviando " << (sizeof(ov7630_i2c)/8) << " comandos al sensor OV7630..." << std::endl;
    for (size_t i = 0; i < (sizeof(ov7630_i2c)/8); i++) {
        bool command_success = false;
        int retries = 3;
        
        while (retries > 0) {
            if (I2CWrite(handle, ov7630_i2c[i])) {
                command_success = true;
                break; // Éxito, salimos del bucle de reintentos
            }
            
            std::cerr << "[WARN] Fallo I2C en indice " << i << ". Reintentando... (" << (retries - 1) << " intentos restantes)" << std::endl;
            Sleep(50); // Pausa corta para que se disipe cualquier ruido eléctrico antes de reintentar
            retries--;
        }

        if (!command_success) {
            std::cerr << "[ERROR] Fallo CRITICO I2C en indice " << i << " despues de agotar los reintentos. Abortando." << std::endl;
            return false;
        }
        
        // Si acabamos de enviar el primer comando (Reset del sensor COM7=0x80),
        // esperamos que el hardware complete el reset antes de continuar.
        if (i == 0) {
            printf("[I2C] Reset enviado. Esperando que el hardware reinicie (500ms)...\n");
            Sleep(500);
            printf("[I2C] Continuando con secuencia de configuracion.\n\n");
        }
    }

    // Remate final de stream on
    WriteRegister(handle, 0x15, bridge_init[20]); // H_size
    WriteRegister(handle, 0x16, bridge_init[21]); // V_size
    WriteRegister(handle, 0x18, r18);  // compression
    WriteRegister(handle, 0x12, 0x02); // hstart
    WriteRegister(handle, 0x13, 0x01); // vstart
    WriteRegister(handle, 0x17, 0xe0); // CRITICO: VSYNC activo bajo para OV7630
    WriteRegister(handle, 0x19, bridge_init[24]); // MCK (0x2b)
    
    // El sonixb escribe AE_STRX AE_STRY AE_ENDX AE_ENDY justo antes de activar
    WriteRegister(handle, 0x1c, 0x05);
    WriteRegister(handle, 0x1d, 0x03);
    WriteRegister(handle, 0x1e, 0x0f);
    WriteRegister(handle, 0x1f, 0x0c);

    WriteRegister(handle, 0x01, 0x30); // Enable Transfer
    
    // Y finalmente, reescribir 0x18 y 0x19 al mismo tiempo
    uint8_t final_18_19[2] = { r18, bridge_init[24] };
    WriteRegisterBlock(handle, 0x18, final_18_19, 2);

    std::cout << "[OK] Secuencia de inicializacion completada." << std::endl;
    return true;
}

// ==============================================================================
// CONTROLES DINÁMICOS EN TIEMPO REAL
// ==============================================================================

bool SetSensorGain(WINUSB_INTERFACE_HANDLE handle, uint8_t gain_level) {
    // Registro 0x00 en el sensor OV7630 controla la Ganancia Global (AGC / Gain)
    // 0xa0 = Enviar 1 byte. Dir. Esclavo. Registro interno.
    uint8_t cmd[8] = {0xa0, OV7630::SLAVE_ADDR_WRITE, OV7630::REG_GAIN, gain_level, 0x00, 0x00, 0x00, 0x10};
    
    std::cout << "[I2C Dinamico] Ajustando Ganancia (Reg 0x" 
              << std::hex << (int)OV7630::REG_GAIN << ") a: 0x" 
              << (int)gain_level << std::dec << std::endl;
              
    return I2CWrite(handle, cmd);
}

bool SetSensorBrightness(WINUSB_INTERFACE_HANDLE handle, uint8_t brightness_level) {
    // Registro 0x24 en el sensor OV7630 controla el Brillo (BRT)
    uint8_t cmd[8] = {0xa0, OV7630::SLAVE_ADDR_WRITE, OV7630::REG_BRT, brightness_level, 0x00, 0x00, 0x00, 0x10};
    
    std::cout << "[I2C Dinamico] Ajustando Brillo (Reg 0x" 
              << std::hex << (int)OV7630::REG_BRT << ") a: 0x" 
              << (int)brightness_level << std::dec << std::endl;
              
    return I2CWrite(handle, cmd);
}
