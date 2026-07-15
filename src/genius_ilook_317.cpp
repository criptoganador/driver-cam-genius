#include "genius_ilook_317.hpp"
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

void InitializeGenius317(WINUSB_INTERFACE_HANDLE winusb_handle) {
    std::cout << "\n=== INICIALIZANDO GENIUS ILOOK 317 (SN9C120) ===" << std::endl;

    // Lista de comandos extraída del driver 'sonixj' de Linux (Sensor TAS5110)
    // Formato: { Registro, Valor }
    struct RegVal { uint16_t reg; uint8_t val; };
    
    std::vector<RegVal> init_sequence = {
        { 0x01, 0x44 }, // SYS_CTRL
        { 0x02, 0x40 }, // CLK_CTRL
        { 0x03, 0x00 }, // VIDEO_CTRL
        { 0x04, 0x1a }, // SYNC_CTRL
        { 0x05, 0x50 }, // PIX_CLK_CTRL
        { 0x06, 0x20 }, // Y_GAIN
        { 0x07, 0x20 }, // R_GAIN
        { 0x08, 0x20 }, // G_GAIN
        { 0x09, 0x20 }, // B_GAIN
        { 0x12, 0x03 }, // V_SIZE
        { 0x13, 0x08 }, // H_SIZE
        { 0x17, 0x02 }, // CLK_OUT
        { 0x18, 0x0a }, // I2C_CTRL
        { 0x1c, 0x00 }, // AE_CTRL
    };

    int fallos = 0;
    for (auto& cmd : init_sequence) {
        if (!WriteRegister(winusb_handle, cmd.reg, cmd.val)) {
            fallos++;
        }
    }
    
    if (fallos == 0) {
        std::cout << "\n[OK] Secuencia de inicializacion completada SIN ERRORES." << std::endl;
    } else {
        std::cout << "\n[ADVERTENCIA] Secuencia completada con " << fallos << " errores." << std::endl;
    }
}
