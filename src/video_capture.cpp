#include "video_capture.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstring>

namespace VideoCapture {

// ─────────────────────────────────────────────────────────────────────────────
// StartStreaming: Activa el DMA del SN9C103
// ─────────────────────────────────────────────────────────────────────────────
bool StartStreaming(WINUSB_INTERFACE_HANDLE handle) {
    std::cout << "\n[STREAM ON] Activando DMA del SN9C103..." << std::endl;

    struct { uint16_t reg; uint8_t val; const char* note; } seq[] = {
        // IMPORTANTE: Usar 0x64 (MCLK/4 = 6 MHz) y NO 0x44 (24 MHz).
        // Con 0x44, el SN9C103 genera más datos de los que USB 1.1 puede transferir.
        { 0x01, 0x64, "Reloj MCLK/4 = 6 MHz (preservar baja velocidad)" },
        { 0x17, 0xe0, "CRITICO: VSYNC activo bajo para OV7630" },
        { 0x01, 0x65, "Bit0=DMA activo hacia USB"      },
    };

    WINUSB_SETUP_PACKET setup{};
    setup.RequestType = 0x41;
    setup.Request     = 0x08;
    setup.Index       = 0;
    setup.Length      = 1;

    for (auto& cmd : seq) {
        setup.Value = cmd.reg;
        uint8_t val = cmd.val;
        ULONG xfer  = 0;
        if (!WinUsb_ControlTransfer(handle, setup, &val, 1, &xfer, nullptr)) {
            printf("[ERROR] Stream On: fallo Reg 0x%02X. Err: %lu\n", cmd.reg, GetLastError());
            return false;
        }
        printf("[STREAM] Reg 0x%02X = 0x%02X  (%s)\n", cmd.reg, cmd.val, cmd.note);
    }

    std::cout << "[STREAM ON] Estabilizando (200ms)..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Guardar buffer como archivo binario
// ─────────────────────────────────────────────────────────────────────────────
static bool SaveFile(const std::string& path, const uint8_t* data, size_t len) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::cerr << "[SAVE] Error creando: " << path << std::endl; return false; }
    f.write(reinterpret_cast<const char*>(data), len);
    printf("[SAVE] %s (%zu bytes)\n", path.c_str(), len);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Guardar Bayer raw como PGM (grayscale viewable)
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Guardar imagen en formato BMP nativo (24-bit RGB) para que Windows lo abra
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct BMPHeader {
    uint16_t bfType{0x4D42}; // "BM"
    uint32_t bfSize{0};
    uint16_t bfReserved1{0};
    uint16_t bfReserved2{0};
    uint32_t bfOffBits{54};
    uint32_t biSize{40};
    int32_t  biWidth{0};
    int32_t  biHeight{0};
    uint16_t biPlanes{1};
    uint16_t biBitCount{24};
    uint32_t biCompression{0};
    uint32_t biSizeImage{0};
    int32_t  biXPelsPerMeter{0};
    int32_t  biYPelsPerMeter{0};
    uint32_t biClrUsed{0};
    uint32_t biClrImportant{0};
};
#pragma pack(pop)

static bool SaveBMP(const std::string& path, const uint8_t* bayer, int w, int h) {
    // BMP requiere que cada fila (scanline) esté alineada a múltiplos de 4 bytes
    int rowStride = (w * 3 + 3) & ~3; 
    std::vector<uint8_t> rgb(rowStride * h, 0);

    // Convertir de Bayer a RGB y amplificar brillo x4
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int i = y * w + x;
            uint8_t r, g, b;
            bool er = (y % 2 == 0), ec = (x % 2 == 0);

            // GBRG: fila par = G B G B ..., fila impar = R G R G ...
            if (er && ec) {
                g = bayer[i]; b = bayer[i - 1]; r = bayer[i - w];
            } else if (er && !ec) {
                b = bayer[i]; 
                g = ((int)bayer[i-1] + bayer[i+1] + bayer[i-w] + bayer[i+w]) / 4;
                r = ((int)bayer[i-w-1] + bayer[i-w+1] + bayer[i+w-1] + bayer[i+w+1]) / 4;
            } else if (!er && ec) {
                r = bayer[i];
                g = ((int)bayer[i-1] + bayer[i+1] + bayer[i-w] + bayer[i+w]) / 4;
                b = ((int)bayer[i-w-1] + bayer[i-w+1] + bayer[i+w-1] + bayer[i+w+1]) / 4;
            } else {
                g = bayer[i]; r = bayer[i - 1]; b = bayer[i - w];
            }

            auto amp = [](int v) -> uint8_t { return (uint8_t)std::min(v * 4, 255); };
            
            // BMP almacena los pixeles de ABAJO hacia ARRIBA y en formato BGR
            int bmpY = (h - 1) - y;
            int ri = bmpY * rowStride + x * 3;
            rgb[ri] = amp(b); rgb[ri+1] = amp(g); rgb[ri+2] = amp(r);
        }
    }

    BMPHeader hdr;
    hdr.biWidth = w;
    hdr.biHeight = h;
    hdr.biSizeImage = rgb.size();
    hdr.bfSize = sizeof(BMPHeader) + hdr.biSizeImage;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(BMPHeader));
    f.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    printf("[SAVE] BMP RGB:   %s (%dx%d)\n", path.c_str(), w, h);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Guardar un frame con detección automática de formato
// ─────────────────────────────────────────────────────────────────────────────
static void SaveFrame(const std::vector<uint8_t>& data, int frameNum, const std::string& dir) {
    std::ostringstream base;
    base << dir << "\\frame_" << std::setw(3) << std::setfill('0') << frameNum;

    bool isJpeg = (data.size() >= 2 && data[0] == 0xFF && data[1] == 0xD8);

    printf("\n[FRAME %03d] %zu bytes — formato: %s\n",
           frameNum, data.size(), isJpeg ? "JPEG" : "RAW Bayer");

    if (isJpeg) {
        // Guardar directamente como .jpg
        SaveFile(base.str() + ".jpg", data.data(), data.size());
    } else {
        // Guardar raw siempre
        SaveFile(base.str() + ".raw", data.data(), data.size());

        // Intentar guardar como imagen si el tamaño coincide con una resolución conocida
        struct { int w; int h; } sizes[] = {
            { 160, 120 },  // 1/4 VGA (RAW)
            { 320, 240 },  // QVGA
            { 352, 288 },  // SIF/CIF
            { 640, 480 },  // VGA
        };

        bool matched = false;
        for (auto& s : sizes) {
            if (data.size() == (size_t)(s.w * s.h)) {
                SaveBMP(base.str() + "_rgb.bmp", data.data(), s.w, s.h);
                matched = true;
                break;
            }
        }
        if (!matched) {
            printf("[FRAME] Tamaño %zu bytes no coincide con resolución conocida.\n", data.size());
            printf("[FRAME] Primeros 8 bytes: ");
            for (int i = 0; i < 8 && i < (int)data.size(); i++) printf("%02X ", data[i]);
            printf("\n");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lanzar una transferencia isócrona y esperar resultado
// ─────────────────────────────────────────────────────────────────────────────
static bool DoIsochRead(WINUSB_ISOCH_BUFFER_HANDLE isochHandle,
                        ULONG bufferLen, BOOL continueStream,
                        std::vector<USBD_ISO_PACKET_DESCRIPTOR>& pkts,
                        int timeoutMs)
{
    ULONG n = (ULONG)pkts.size();
    for (auto& p : pkts) { p.Offset = 0; p.Length = 0; p.Status = 0; }

    OVERLAPPED ov{};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    BOOL ok = WinUsb_ReadIsochPipeAsap(isochHandle, 0, bufferLen,
                                        continueStream, n, pkts.data(), &ov);

    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        return false;
    }

    DWORD wr = WaitForSingleObject(ov.hEvent, timeoutMs);
    CloseHandle(ov.hEvent);

    if (wr == WAIT_TIMEOUT) return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// CaptureFrames: Loop de captura usando detección de SOF por paquete
//
// ALGORITMO CORRECTO (idéntico a sd_pkt_scan en sonixb.c de Linux):
//   - Revisar CADA PAQUETE individualmente
//   - Si el paquete COMIENZA con FF FF → es inicio de nuevo frame
//   - Caso contrario → es continuación del frame actual
//
// Esto evita los falsos positivos de buscar FF FF en el stream acumulado.
// ─────────────────────────────────────────────────────────────────────────────
int CaptureFrames(WINUSB_INTERFACE_HANDLE handle, UCHAR endpointId,
                  int numFrames, const std::string& outputDir)
{
    std::cout << "\n------------------------------------------------------------" << std::endl;
    std::cout << "       CAPTURA CONTINUA DE FRAMES (SOF por paquete)      " << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;

    // ── RAW_IO (puede fallar en algunos setups — continuamos de todos modos) ──
    ULONG rawIo = 1;
    WinUsb_SetPipePolicy(handle, endpointId, RAW_IO, sizeof(rawIo), &rawIo);

    // ── Parámetros ─────────────────────────────────────────────────────────────
    const ULONG MAX_PKT    = 1003;          // MaxPacketSize en Setting 8
    const ULONG NUM_PKTS   = 512;           // paquetes por lectura (~512ms de datos)
    const ULONG BUF_SIZE   = MAX_PKT * NUM_PKTS;  // ~500KB

    std::vector<UCHAR> dmaBuf(BUF_SIZE, 0);
    WINUSB_ISOCH_BUFFER_HANDLE isochHandle = nullptr;

    if (!WinUsb_RegisterIsochBuffer(handle, endpointId,
                                    dmaBuf.data(), BUF_SIZE, &isochHandle)) {
        std::cerr << "[CAPTURE] RegisterIsochBuffer fallo. Err: " << GetLastError() << std::endl;
        return 0;
    }

    printf("[CAPTURE] Buffer DMA: %lu KB | %lu paquetes x %lu bytes\n",
           BUF_SIZE / 1024, NUM_PKTS, MAX_PKT);

    std::vector<USBD_ISO_PACKET_DESCRIPTOR> pkts(NUM_PKTS);

    // ── Estado del frame actual ────────────────────────────────────────────────
    std::vector<uint8_t> curFrame;
    curFrame.reserve(640 * 480 + 1024);  // Reservar para VGA
    bool inFrame     = false;
    int  savedFrames = 0;
    int  batchNum    = 0;
    bool firstRead   = true;
    const ULONG SOF_SKIP = 18;  // SN9C103 usa un header de 18 bytes total (6 marker + 12 payload)
                                  // Fuente: sd_pkt_scan en sonixb.c (fr_h_sz = 18 para BRIDGE_103)

    const int MAX_BATCHES = 200;  // límite de seguridad

    while (savedFrames < numFrames && batchNum < MAX_BATCHES) {
        batchNum++;

        bool ok = DoIsochRead(isochHandle, BUF_SIZE,
                              !firstRead, pkts, 5000);
        firstRead = false;

        if (!ok) {
            printf("[CAPTURE] Batch %d: timeout/error — reintentando desde cero\n", batchNum);
            firstRead = true;
            continue;
        }

        // ── Procesar cada paquete individualmente ──────────────────────────────
        int batchBytes = 0;
        int batchPkts  = 0;

        for (ULONG i = 0; i < NUM_PKTS; i++) {
            if (pkts[i].Length == 0) continue;

            // Localizar datos del paquete en el buffer DMA
            // Los paquetes están a offsets fijos de i * MAX_PKT
            uint8_t* pkt = dmaBuf.data() + (i * MAX_PKT);
            ULONG    len = pkts[i].Length;

            batchBytes += len;
            batchPkts++;

            // ── Detectar inicio de frame (SOF): el paquete EMPIEZA con la firma ──
            // Firma SN9C103: FF FF 00 C4 C4 96
            bool isSOF = (len >= 6 && 
                          pkt[0] == 0xFF && pkt[1] == 0xFF &&
                          pkt[2] == 0x00 && pkt[3] == 0xC4 && 
                          pkt[4] == 0xC4 && pkt[5] == 0x96);

            if (isSOF) {
                // Finalizar frame anterior si tiene datos suficientes
                if (inFrame && curFrame.size() > 256) {
                    // Anlisis del frame antes de guardar
                    bool isJpeg = (curFrame.size() >= 2 &&
                                   curFrame[0] == 0xFF && curFrame[1] == 0xD8);
                    printf("[FRAME %03d] %zu bytes | formato: %s | 1ros bytes: ",
                           savedFrames + 1, curFrame.size(),
                           isJpeg ? "JPEG" : "RAW/JPGH");
                    for (int b = 0; b < 8 && b < (int)curFrame.size(); b++)
                        printf("%02X ", curFrame[b]);
                    printf("\n");

                    SaveFrame(curFrame, savedFrames + 1, outputDir);
                    savedFrames++;
                    if (savedFrames >= numFrames) break;
                }

                // Nuevo frame: skip total = 18 bytes desde inicio
                // Esto replica exactamente fr_h_sz = 18 en sd_pkt_scan de sonixb.c para SN9C103
                curFrame.clear();
                inFrame = true;
                if (len > SOF_SKIP) {
                    curFrame.insert(curFrame.end(), pkt + SOF_SKIP, pkt + len);
                }
            } else if (inFrame) {
                // Paquete de continuación — agregar datos al frame actual
                curFrame.insert(curFrame.end(), pkt, pkt + len);
            }
        }

        printf("[CAPTURE] Batch %3d: %4d pkts | +%6d bytes | Frame actual: %7zu bytes\n",
               batchNum, batchPkts, batchBytes, curFrame.size());
    }

    // Guardar último frame si tiene datos
    if (inFrame && curFrame.size() > 512 && savedFrames < numFrames) {
        SaveFrame(curFrame, savedFrames + 1, outputDir);
        savedFrames++;
    }

    WinUsb_UnregisterIsochBuffer(isochHandle);

    std::cout << "\n------------------------------------------------------------" << std::endl;
    printf(    "   Frames guardados: %2d / %2d\n",
               savedFrames, numFrames);
    std::cout << "------------------------------------------------------------" << std::endl;

    return savedFrames;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestStream: Diagnóstico rápido — una sola transferencia
// ─────────────────────────────────────────────────────────────────────────────
bool TestStream(WINUSB_INTERFACE_HANDLE handle, UCHAR endpointId) {
    std::cout << "\n[TEST] Lectura rapida en Endpoint 0x"
              << std::hex << (int)endpointId << std::dec << std::endl;

    ULONG rawIo = 1;
    WinUsb_SetPipePolicy(handle, endpointId, RAW_IO, sizeof(rawIo), &rawIo);

    const ULONG PS = 1003, NP = 128, BS = PS * NP;
    std::vector<UCHAR> buf(BS, 0);
    WINUSB_ISOCH_BUFFER_HANDLE h = nullptr;
    if (!WinUsb_RegisterIsochBuffer(handle, endpointId, buf.data(), BS, &h)) return false;

    std::vector<USBD_ISO_PACKET_DESCRIPTOR> pkts(NP);
    for (auto& p : pkts) p = {};
    OVERLAPPED ov{}; ov.hEvent = CreateEvent(nullptr,TRUE,FALSE,nullptr);
    WinUsb_ReadIsochPipeAsap(h, 0, BS, FALSE, NP, pkts.data(), &ov);
    WaitForSingleObject(ov.hEvent, 5000);
    CloseHandle(ov.hEvent);

    int total=0, full=0;
    for (ULONG i=0;i<NP;i++) { if(pkts[i].Length>0){total+=pkts[i].Length;full++;} }
    printf("[TEST] %d bytes en %d/%lu paquetes\n", total, full, NP);
    if (total > 0) {
        printf("[TEST] Primeros 16 bytes del paquete 0: ");
        for (int i=0;i<16&&i<(int)buf.size();i++) printf("%02X ",buf[i]);
        printf("\n");
    }

    WinUsb_UnregisterIsochBuffer(h);
    return total > 0;
}

} // namespace VideoCapture
