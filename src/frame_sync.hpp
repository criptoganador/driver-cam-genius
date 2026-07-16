#pragma once
#include <windows.h>
#include <winusb.h>
#include <cstdint>
#include <array>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// Máquina de Estados de Sincronización de Frame
// ─────────────────────────────────────────────────────────────────────────────
// windows.h define ERROR como macro numérica — la eliminamos para que el enum
// pueda usar nombres descriptivos sin conflicto con el preprocesador.
#ifdef ERROR
#  undef ERROR
#endif

enum class SyncState {
    IDLE,               // Esperando el marcador VSYNC (FF FF 00 C4 C4 96)
    CAPTURING,          // VSYNC detectado, acumulando datos de píxeles
    TRANSFER_COMPLETE,  // Frame completo — listo para swap de buffer
    SYNC_ERROR          // Timeout: sensor sin respuesta > VSYNC_TIMEOUT_MS
};

// ─────────────────────────────────────────────────────────────────────────────
// FrameSyncManager — Doble Buffer + Hilo Dedicado + Watchdog
//
// Uso:
//   FrameSyncManager sync(handle, 0x81);
//   sync.Start([](const std::vector<uint8_t>& frame) {
//       // Este callback se llama cada vez que un frame completo está listo
//       // Se ejecuta en el hilo de captura, guarda o copia rápido.
//   });
//   ...
//   sync.Stop();
// ─────────────────────────────────────────────────────────────────────────────
class FrameSyncManager {
public:
    // Firma del SOF que usa el SN9C103 (Sonix Bridge) para señalar VSYNC
    static constexpr uint8_t SOF_MARKER[6] = {0xFF, 0xFF, 0x00, 0xC4, 0xC4, 0x96};
    static constexpr size_t  SOF_MARKER_LEN = 6;
    static constexpr size_t  SOF_HEADER_SKIP = 18; // Bytes totales de cabecera SN9C103

    // Timeout de watchdog: si no llega VSYNC en 500ms, el sensor se considera muerto
    static constexpr int VSYNC_TIMEOUT_MS = 500;

    // Callback que se llama cuando un frame está completo (desde el hilo de captura)
    using FrameReadyCallback = std::function<void(const std::vector<uint8_t>&)>;

    FrameSyncManager(WINUSB_INTERFACE_HANDLE handle, UCHAR endpoint_id)
        : handle_(handle), endpoint_id_(endpoint_id),
          write_buf_(0), state_(SyncState::IDLE), running_(false), frame_count_(0) {}

    ~FrameSyncManager() { Stop(); }

    // Inicia el hilo de captura con un callback opcional por frame completo
    void Start(FrameReadyCallback callback = nullptr) {
        if (running_) return;
        callback_ = callback;
        running_ = true;
        last_vsync_ = std::chrono::steady_clock::now();
        capture_thread_ = std::jthread([this]() { CaptureLoop(); });
        std::cout << "[SYNC] Hilo de captura iniciado." << std::endl;
    }

    // Detiene el hilo de captura limpiamente
    void Stop() {
        running_ = false;
        if (capture_thread_.joinable()) capture_thread_.join();
        std::cout << "[SYNC] Hilo de captura detenido." << std::endl;
    }

    // Obtiene una COPIA del último frame completo (hilo-seguro para la UI)
    std::vector<uint8_t> GetLatestFrame() {
        std::lock_guard<std::mutex> lock(read_mutex_);
        // El buffer de lectura es el que NO está siendo escrito ahora mismo
        int read_buf = 1 - write_buf_;
        return buffers_[read_buf];
    }

    // Estadísticas de estado
    SyncState GetState() const { return state_; }
    int       GetFrameCount() const { return frame_count_; }
    bool      IsRunning() const { return running_; }

private:
    WINUSB_INTERFACE_HANDLE handle_;
    UCHAR                   endpoint_id_;
    FrameReadyCallback      callback_;

    // Doble Buffer — uno para escribir (captura), uno para leer (UI)
    std::array<std::vector<uint8_t>, 2> buffers_;
    int                 write_buf_;   // Índice del buffer activo de escritura
    std::mutex          read_mutex_;  // Protege el swap entre buffers

    // Máquina de estados
    std::atomic<SyncState> state_;
    std::atomic<bool>      running_;
    std::atomic<int>       frame_count_;

    // Watchdog
    std::chrono::steady_clock::time_point last_vsync_;

    // Hilo (jthread auto-join al destruir)
    std::jthread capture_thread_;

    // Buffer de trabajo para el frame en construcción
    std::vector<uint8_t> working_frame_;

    // ─────────────────────────────────────────────────────────────────────────
    // FindSOF — Busca el marcador FF FF 00 C4 C4 96 en cualquier posición
    // del paquete (igual que find_sof en el kernel Linux sonixb.c).
    // Retorna el índice donde comienza el marcador, o -1 si no se encontró.
    // ─────────────────────────────────────────────────────────────────────────
    int FindSOF(const uint8_t* pkt, size_t len) {
        if (len < SOF_MARKER_LEN) return -1;
        for (size_t i = 0; i <= len - SOF_MARKER_LEN; ++i) {
            if (pkt[i]   == SOF_MARKER[0] &&
                pkt[i+1] == SOF_MARKER[1] &&
                pkt[i+2] == SOF_MARKER[2] &&
                pkt[i+3] == SOF_MARKER[3] &&
                pkt[i+4] == SOF_MARKER[4] &&
                pkt[i+5] == SOF_MARKER[5]) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // ProcessPacket — Algoritmo exacto de sonixb.c (sd_pkt_scan):
    //
    // Dado un paquete isócrono USB:
    //  1. Buscar el marcador SOF *en cualquier posición* del paquete.
    //  2. Los bytes ANTES del SOF son el FINAL del frame anterior → añadir y cerrar.
    //  3. Los bytes DESPUÉS del SOF (saltando la cabecera de 18 bytes) son el
    //     INICIO del nuevo frame → empezar a acumular.
    // ─────────────────────────────────────────────────────────────────────────
    void ProcessPacket(const uint8_t* pkt, size_t len) {
        if (len == 0) return;

        int sof_pos = FindSOF(pkt, len);

        if (sof_pos >= 0) {
            // ── SOF ENCONTRADO ────────────────────────────────────────────────
            last_vsync_ = std::chrono::steady_clock::now();

            // Paso 1: Datos ANTES del SOF → final del frame en construcción
            size_t bytes_before_sof = static_cast<size_t>(sof_pos);
            if (state_ == SyncState::CAPTURING && bytes_before_sof > 0) {
                working_frame_.insert(working_frame_.end(), pkt, pkt + bytes_before_sof);
            }

            // Paso 2: Cerrar frame anterior si tiene suficientes datos
            if (state_ == SyncState::CAPTURING && working_frame_.size() > 1024) {
                CommitFrame();
            } else if (state_ == SyncState::CAPTURING) {
                // Frame demasiado pequeño → descartar silenciosamente
                working_frame_.clear();
            }

            // Paso 3: Comenzar nuevo frame con los bytes DESPUÉS de la cabecera (18 bytes)
            working_frame_.clear();
            state_ = SyncState::CAPTURING;

            size_t header_end = static_cast<size_t>(sof_pos) + SOF_HEADER_SKIP;
            if (header_end < len) {
                working_frame_.insert(working_frame_.end(), pkt + header_end, pkt + len);
            }

        } else if (state_ == SyncState::CAPTURING) {
            // ── CONTINUACIÓN DE FRAME: añadir todos los bytes al frame actual ──
            working_frame_.insert(working_frame_.end(), pkt, pkt + len);
        }
        // Si state_ == IDLE y no hay SOF: datos basura pre-sincronización → descartar
    }

    // ─────────────────────────────────────────────────────────────────────────
    // CommitFrame — Hace el swap de buffers y notifica al callback
    // ─────────────────────────────────────────────────────────────────────────
    void CommitFrame() {
        int next_write = 1 - write_buf_;

        {
            std::lock_guard<std::mutex> lock(read_mutex_);
            // Mover el frame al buffer de escritura (operación barata, no copia)
            buffers_[write_buf_] = std::move(working_frame_);
            write_buf_ = next_write; // Swap atómico del índice
        }

        working_frame_.clear();
        working_frame_.reserve(640 * 480);

        int current_count = ++frame_count_;
        state_ = SyncState::TRANSFER_COMPLETE;

        size_t frame_sz = buffers_[1 - write_buf_].size();
        // 320x240 = 76800 bytes sin comprimir; con DPCM ~10000-30000 bytes es normal
        printf("[SYNC] Frame #%d | %zu bytes (esperado ~76800 RAW o ~10000-30000 DPCM)\n",
               current_count, frame_sz);

        // Llamar al callback si existe (ej. guardar a disco o renderizar)
        if (callback_) {
            callback_(buffers_[1 - write_buf_]);
        }

        state_ = SyncState::IDLE;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // CheckWatchdog — Detecta si el sensor se colgó
    // ─────────────────────────────────────────────────────────────────────────
    bool CheckWatchdog() {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_vsync_).count();

        if (elapsed > VSYNC_TIMEOUT_MS && state_ == SyncState::CAPTURING) {
            std::cerr << "[SYNC][WATCHDOG] Sin VSYNC por " << elapsed
                      << "ms. El sensor puede estar colgado." << std::endl;
            state_ = SyncState::SYNC_ERROR;
            return false; // Señal de error al bucle principal
        }
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // CaptureLoop — Bucle principal del hilo de captura
    // ─────────────────────────────────────────────────────────────────────────
    void CaptureLoop() {
        const ULONG MAX_PKT  = 1003;
        const ULONG NUM_PKTS = 512;
        const ULONG BUF_SIZE = MAX_PKT * NUM_PKTS; // ~500 KB

        // Configurar RAW_IO en el endpoint
        ULONG rawIo = 1;
        WinUsb_SetPipePolicy(handle_, endpoint_id_, RAW_IO, sizeof(rawIo), &rawIo);

        // Registrar buffer DMA isócrono
        std::vector<UCHAR> dma_buf(BUF_SIZE, 0);
        WINUSB_ISOCH_BUFFER_HANDLE isoch = nullptr;
        if (!WinUsb_RegisterIsochBuffer(handle_, endpoint_id_,
                                        dma_buf.data(), BUF_SIZE, &isoch)) {
            std::cerr << "[SYNC] ERROR: No se pudo registrar buffer isócrono. Err: "
                      << GetLastError() << std::endl;
            running_ = false;
            return;
        }

        std::vector<USBD_ISO_PACKET_DESCRIPTOR> pkts(NUM_PKTS);
        bool first_read = true;

        printf("[SYNC] Buffer DMA: %lu KB | %lu paquetes x %lu bytes\n",
               BUF_SIZE / 1024, NUM_PKTS, MAX_PKT);
        std::cout << "[SYNC] Estado: IDLE → esperando VSYNC..." << std::endl;

        working_frame_.reserve(640 * 480);
        last_vsync_ = std::chrono::steady_clock::now();

        while (running_) {
            // Verificar watchdog antes de cada lectura
            if (!CheckWatchdog() && state_ == SyncState::SYNC_ERROR) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                state_ = SyncState::IDLE; // Intentar recuperar
                last_vsync_ = std::chrono::steady_clock::now();
                continue;
            }

            // ── Lanzar transferencia isócrona ─────────────────────────────────
            for (auto& p : pkts) { p.Offset = 0; p.Length = 0; p.Status = 0; }

            OVERLAPPED ov{};
            ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            BOOL ok = WinUsb_ReadIsochPipeAsap(isoch, 0, BUF_SIZE,
                                               !first_read, NUM_PKTS,
                                               pkts.data(), &ov);
            first_read = false;

            if (!ok && GetLastError() != ERROR_IO_PENDING) {
                CloseHandle(ov.hEvent);
                printf("[SYNC] Error de transferencia: %lu. Reintentando...\n", GetLastError());
                first_read = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            DWORD wait = WaitForSingleObject(ov.hEvent, 3000);
            CloseHandle(ov.hEvent);

            if (wait == WAIT_TIMEOUT) {
                printf("[SYNC] Timeout de transferencia USB. Reintentando...\n");
                first_read = true;
                continue;
            }

            // ── Procesar cada paquete con la máquina de estados ───────────────
            for (ULONG i = 0; i < NUM_PKTS && running_; i++) {
                if (pkts[i].Length == 0) continue;
                ProcessPacket(dma_buf.data() + (i * MAX_PKT), pkts[i].Length);
            }
        }

        WinUsb_UnregisterIsochBuffer(isoch);
    }
};
