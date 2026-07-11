#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <SDL2/SDL.h>

// Definición estricta de las capacidades del hardware Genius
constexpr int SCREEN_WIDTH = 640;
constexpr int SCREEN_HEIGHT = 480;
constexpr int BYTES_PER_PIXEL = 3; // RGB888 (24-bit)

class VideoPlayer {
private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    bool is_running = false;

public:
    VideoPlayer() {
        // Inicializar el subsistema de video de bajo nivel de SDL2
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            throw std::runtime_error("Fallo critico al inicializar SDL2: " + std::string(SDL_GetError()));
        }

        // Crear la ventana nativa del sistema operativo
        window = SDL_CreateWindow(
            "Genius VideoCAM GE111 - Reproductor Nativo C++",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            SCREEN_WIDTH, SCREEN_HEIGHT,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
        );

        if (!window) {
            throw std::runtime_error("No se pudo crear la ventana: " + std::string(SDL_GetError()));
        }

        // Crear el renderizador acelerado por GPU (Direct3D o OpenGL segun el OS)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            throw std::runtime_error("Fallo la aceleracion por hardware: " + std::string(SDL_GetError()));
        }

        // Crear la textura de streaming donde se estampara el buffer de WinUSB
        texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGB24, // Formato crudo directo del de-mosaicado
            SDL_TEXTUREACCESS_STREAMING, // Acceso dinamico optimizado para cambios frame a frame
            SCREEN_WIDTH, SCREEN_HEIGHT
        );

        if (!texture) {
            throw std::runtime_error("Error al mapear la textura de streaming: " + std::string(SDL_GetError()));
        }

        is_running = true;
    }

    ~VideoPlayer() {
        // Liberacion estricta de recursos de memoria de video para evitar fugas
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    // Deshabilitar copias para proteger los punteros del hardware
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    bool isOpen() const { return is_running; }

    void manejarEventos() {
        SDL_Event evento;
        while (SDL_PollEvent(&evento)) {
            if (evento.type == SDL_QUIT) {
                is_running = false;
            }
        }
    }

    // Inyeccion atomica del buffer de bytes crudos a la GPU
    void actualizarFrame(const unsigned char* pixelData) {
        if (!pixelData) return;

        // Actualizar los pixeles de la textura directamente en la VRAM
        // El "Pitch" es el ancho de la imagen en bytes (640 * 3 = 1920 bytes por linea)
        SDL_UpdateTexture(texture, nullptr, pixelData, SCREEN_WIDTH * BYTES_PER_PIXEL);

        // Limpiar el backbuffer del renderizador
        SDL_RenderClear(renderer);

        // Copiar la textura actualizada al lienzo de la ventana
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);

        // Intercambiar buffers para mostrar la imagen en pantalla (Evita el parpadeo)
        SDL_RenderPresent(renderer);
    }
};

// Simulacion del bucle principal conectado al hilo de WinUSB
int main(int argc, char* argv[]) {
    try {
        VideoPlayer reproductor;

        // Buffer de prueba: simula el tamaño exacto de un frame RGB de 640x480
        std::vector<unsigned char> bufferFalsoRGB(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);
        unsigned char tonoEstatico = 0;

        std::cout << "[+] Motor de renderizado C++ activo. Ejecutando bucle de video..." << std::endl;

        while (reproductor.isOpen()) {
            reproductor.manejarEventos();

            // --- NOTA DE INTEGRACION ---
            // Aqui es donde inyectas el buffer real que viene del script asincrono de WinUSB:
            // TuLogica_WinUsb_ReadIsochPipe(..., bufferFalsoRGB.data());
            
            // Simulacion de ruido/movimiento variando el canal de color del buffer
            tonoEstatico = (tonoEstatico + 1) % 255;
            std::fill(bufferFalsoRGB.begin(), bufferFalsoRGB.end(), tonoEstatico);

            // Enviar los bytes crudos a la pantalla
            reproductor.actualizarFrame(bufferFalsoRGB.data());

            // Forzar descanso de hilo para sincronizar a ~60 FPS reales
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[-] Error en el proceso de renderizado: " << e.what() << std::endl;
        return -1;
    }

    std::cout << "[+] Reproductor cerrado de forma segura." << std::endl;
    return 0;
}