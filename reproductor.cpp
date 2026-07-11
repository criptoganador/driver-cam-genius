#include <windows.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <SDL2/SDL.h>

// =======================================================================
// CAPA DE DATOS: Gestión No Bloqueante del Canal IPC (Named Pipe)
// =======================================================================
class VideoPipeServer {
private:
    HANDLE hPipe;
    const DWORD CHUNK_SIZE = 6800;

public:
    VideoPipeServer() : hPipe(INVALID_HANDLE_VALUE) {}

    bool inicializar() {
        std::cout << "[...] Creando servidor de tuberia virtual (Named Pipe)..." << std::endl;

        hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\GeniusLookVideoPipe", 
            PIPE_ACCESS_INBOUND,                            
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 
            1,                                              
            CHUNK_SIZE,                                    
            CHUNK_SIZE,                                    
            0,                                              
            NULL                                            
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "[-] Error critico al instanciar el Pipe Win32. Codigo: " << GetLastError() << std::endl;
            return false;
        }

        std::cout << "[+] Pipe en escucha. Esperando conexion de capturar_isocrono_async.ps1..." << std::endl;
        
        BOOL conectado = ConnectNamedPipe(hPipe, NULL);
        
        if (!conectado && GetLastError() != ERROR_PIPE_CONNECTED) {
            std::cerr << "[-] Error de sincronizacion en el handshake del Pipe." << std::endl;
            CloseHandle(hPipe);
            return false;
        }

        std::cout << "[+] ¡PowerShell enlazado! Sincronia de memoria establecida con exito." << std::endl;
        return true;
    }

    bool leerChunkNoBloqueante(std::vector<char>& chunkBuffer, DWORD& bytesLeidos) {
        DWORD bytesDisponibles = 0;
        
        if (!PeekNamedPipe(hPipe, NULL, 0, NULL, &bytesDisponibles, NULL)) {
            return false; 
        }

        if (bytesDisponibles == 0) {
            bytesLeidos = 0;
            return true; 
        }

        DWORD aLeer = (bytesDisponibles > chunkBuffer.size()) ? chunkBuffer.size() : bytesDisponibles;
        BOOL resultado = ReadFile(hPipe, chunkBuffer.data(), aLeer, &bytesLeidos, NULL);
        return (resultado && bytesLeidos > 0);
    }

    void cerrar() {
        if (hPipe != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            std::cout << "[i] Canal IPC cerrado y desvinculado de la RAM." << std::endl;
        }
    }
};

// =======================================================================
// CAPA DE PRESENTACIÓN: Control de Ventana y Texturas en VRAM
// =======================================================================
class VentanaRenderizado {
private:
    SDL_Window* ventana;
    SDL_Renderer* renderizador;
    SDL_Texture* texturaVideo;
    const int ANCHO = 640;  
    const int ALTO = 480;

public:
    VentanaRenderizado() : ventana(nullptr), renderizador(nullptr), texturaVideo(nullptr) {}

    bool inicializar() {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "[-] Error al inicializar SDL2: " << SDL_GetError() << std::endl;
            return false;
        }

        ventana = SDL_CreateWindow(
            "Driver Genius Look - Renderizador Activo",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            ANCHO, ALTO,
            SDL_WINDOW_SHOWN
        );

        if (!ventana) {
            std::cerr << "[-] Error al crear la ventana de SDL2: " << SDL_GetError() << std::endl;
            return false;
        }

        renderizador = SDL_CreateRenderer(ventana, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderizador) {
            std::cerr << "[-] Error al crear el renderizador GPU: " << SDL_GetError() << std::endl;
            return false;
        }

        texturaVideo = SDL_CreateTexture(
            renderizador,
            SDL_PIXELFORMAT_BGR24, 
            SDL_TEXTUREACCESS_STREAMING,
            ANCHO, ALTO
        );

        if (!texturaVideo) {
            std::cerr << "[-] Error al instanciar la textura de GPU: " << SDL_GetError() << std::endl;
            return false;
        }

        return true;
    }

    void procesarFrame(const std::vector<char>& frameBuffer) {
        SDL_UpdateTexture(texturaVideo, NULL, frameBuffer.data(), ANCHO * 3);
        SDL_RenderClear(renderizador);
        SDL_RenderCopy(renderizador, texturaVideo, NULL, NULL);
        SDL_RenderPresent(renderizador);
    }

    void liberar() {
        if (texturaVideo) SDL_DestroyTexture(texturaVideo);
        if (renderizador) SDL_DestroyRenderer(renderizador);
        if (ventana) SDL_DestroyWindow(ventana);
        SDL_Quit();
        std::cout << "[i] Subsistema grafico SDL2 liberado." << std::endl;
    }
};

// =======================================================================
// CAPA DE CONTROL PRINCIPAL: Ciclo Asíncrono con Telemetría Activa
// =======================================================================
int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    
    VideoPipeServer pipeServer;
    VentanaRenderizado visualizador;

    if (!pipeServer.inicializar() || !visualizador.inicializar()) {
        pipeServer.cerrar();
        return -1;
    }

    std::cout << "[+] Motor grafico activo. Monitoreando telemetria del flujo..." << std::endl;

    std::vector<char> chunkBuffer(6800);
    const size_t TOTAL_FRAME_SIZE = 640 * 480 * 3; 
    std::vector<char> frameBuffer(TOTAL_FRAME_SIZE, 0);
    
    size_t acumulados = 0;
    bool running = true;
    SDL_Event evento;

    // Variables para el cálculo de telemetría (Bytes por segundo)
    DWORD totalBytesSg = 0;
    DWORD framesSg = 0;
    ULONGLONG ultimoReloj = GetTickCount64();

    while (running) {
        while (SDL_PollEvent(&evento)) {
            if (evento.type == SDL_QUIT) {
                running = false;
            }
        }

        DWORD bytesLeidos = 0;
        if (pipeServer.leerChunkNoBloqueante(chunkBuffer, bytesLeidos)) {
            if (bytesLeidos > 0) {
                totalBytesSg += bytesLeidos;

                size_t espacioLibre = TOTAL_FRAME_SIZE - acumulados;
                size_t aCopiar = (bytesLeidos > espacioLibre) ? espacioLibre : bytesLeidos;

                std::memcpy(frameBuffer.data() + acumulados, chunkBuffer.data(), aCopiar);
                acumulados += aCopiar;

                if (acumulados >= TOTAL_FRAME_SIZE) {
                    visualizador.procesarFrame(frameBuffer);
                    framesSg++;
                    acumulados = 0; 

                    size_t residuo = bytesLeidos - aCopiar;
                    if (residuo > 0) {
                        std::memcpy(frameBuffer.data(), chunkBuffer.data() + aCopiar, residuo);
                        acumulados = residuo;
                    }
                }
            } else {
                SDL_Delay(1); 
            }
        } else {
            std::cerr << "\n[-] Flujo IPC interrumpido desde la consola de captura." << std::endl;
            running = false;
        }

        // Renderizar logs de telemetría en consola cada 1000 milisegundos (1 segundo)
        ULONGLONG relojActual = GetTickCount64();
        if (relojActual - ultimoReloj >= 1000) {
            double megabytes = (double)totalBytesSg / (1024.0 * 1024.0);
            std::cout << "\r[TELEMETRIA] Velocidad: " << megabytes << " MB/s | Frames Procesados: " << framesSg 
                      << " | Bytes Acumulados en Buffer: " << acumulados << "    " << std::flush;
            
            // Resetear contadores del segundo actual
            totalBytesSg = 0;
            framesSg = 0;
            ultimoReloj = relojActual;
        }
    }

    visualizador.liberar();
    pipeServer.cerrar();
    return 0;
}