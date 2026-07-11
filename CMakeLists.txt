cmake_minimum_required(VERSION 3.14)
project(GeniusLookDriver VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Traer SDL2 mediante FetchContent (descarga y compila automáticamente)
include(FetchContent)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.2
)
FetchContent_MakeAvailable(SDL2)

# 2. El ejecutable original de descubrimiento SetupAPI
add_executable(BuscadorCamara src/main.cpp)
target_link_libraries(BuscadorCamara PRIVATE setupapi)

# 3. El nuevo reproductor de video nativo C++
add_executable(SimuladorVideo VideoPlayer.cpp)
target_link_libraries(SimuladorVideo PRIVATE SDL2::SDL2main SDL2::SDL2-static)
