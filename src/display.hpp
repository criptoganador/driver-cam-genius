#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// display.hpp — Módulo de visualización en tiempo real con SDL2
// ─────────────────────────────────────────────────────────────────────────────
// Uso:
//   Display::Window win(320, 240, "Genius iLook 317");
//   win.UpdateFrame(bgr_data, 320, 240);   // llamar en cada frame
//   while (win.ProcessEvents()) { ... }    // false = usuario cerró la ventana
// ─────────────────────────────────────────────────────────────────────────────

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>

namespace Display {

class Window {
public:
    // ── Constructor: abre la ventana SDL2 ────────────────────────────────────
    Window(int w, int h, const char* title)
        : width_(w), height_(h), alive_(false),
          window_(nullptr), renderer_(nullptr), texture_(nullptr),
          frames_(0), fps_(0.0f)
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            printf("[SDL2] SDL_Init fallo: %s\n", SDL_GetError());
            return;
        }

        // Ventana 2x para que se vea bien en monitores modernos (320->640)
        window_ = SDL_CreateWindow(
            title,
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            w * 2, h * 2,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
        );
        if (!window_) {
            printf("[SDL2] CreateWindow fallo: %s\n", SDL_GetError());
            return;
        }

        renderer_ = SDL_CreateRenderer(window_, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) {
            printf("[SDL2] CreateRenderer fallo: %s\n", SDL_GetError());
            return;
        }

        // SDL_PIXELFORMAT_BGR24: los datos del Debayering ya vienen en BGR
        texture_ = SDL_CreateTexture(renderer_,
            SDL_PIXELFORMAT_BGR24,
            SDL_TEXTUREACCESS_STREAMING,
            w, h);
        if (!texture_) {
            printf("[SDL2] CreateTexture fallo: %s\n", SDL_GetError());
            return;
        }

        SDL_RenderSetLogicalSize(renderer_, w, h);
        alive_ = true;
        t_last_ = std::chrono::steady_clock::now();
        printf("[SDL2] Ventana abierta: %dx%d (zoom x2). Presiona ESC para salir.\n", w * 2, h * 2);
    }

    ~Window() {
        if (texture_)  SDL_DestroyTexture(texture_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_)   SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    bool IsAlive() const { return alive_; }

    // ── Sube el frame BGR24 a la textura GPU y lo pinta en pantalla ──────────
    bool UpdateFrame(const std::vector<uint8_t>& bgr, int w, int h) {
        if (!alive_) return false;

        // Lock the streaming texture for CPU write
        void* pixels = nullptr;
        int   pitch  = 0;
        if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch) != 0) {
            printf("[SDL2] LockTexture fallo: %s\n", SDL_GetError());
            return false;
        }

        // pitch puede diferir de w*3 por alineación de GPU. Copiar fila a fila.
        for (int row = 0; row < h; ++row) {
            const uint8_t* src = bgr.data() + static_cast<size_t>(row * w * 3);
            uint8_t*       dst = static_cast<uint8_t*>(pixels) + row * pitch;
            std::memcpy(dst, src, static_cast<size_t>(w * 3));
        }
        SDL_UnlockTexture(texture_);

        // Dibujar textura a pantalla completa
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);

        // ── FPS counter en el título de la ventana ───────────────────────────
        ++frames_;
        auto  now     = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - t_last_).count();
        if (elapsed >= 1.0f) {
            fps_    = static_cast<float>(frames_) / elapsed;
            frames_ = 0;
            t_last_ = now;
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "Genius iLook 317  |  %.1f FPS  |  %dx%d", fps_, w, h);
            SDL_SetWindowTitle(window_, buf);
        }

        return true;
    }

    // ── Procesar eventos SDL (retorna false si usuario cierra la ventana) ────
    bool ProcessEvents() {
        if (!alive_) return false;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                alive_ = false;
                return false;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                alive_ = false;
                return false;
            }
        }
        return true;
    }

    float GetFPS() const { return fps_; }

private:
    int   width_, height_;
    bool  alive_;
    SDL_Window*   window_;
    SDL_Renderer* renderer_;
    SDL_Texture*  texture_;
    int   frames_;
    float fps_;
    std::chrono::steady_clock::time_point t_last_;
};

} // namespace Display
