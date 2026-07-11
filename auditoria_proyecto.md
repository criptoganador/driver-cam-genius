# Auditoría Profunda del Proyecto: driver-genius-look

Este proyecto es un **controlador (driver) de espacio de usuario escrito a medida (custom)** para una cámara web Genius (específicamente basada en el puente Sonix SN9C105/SN9C120 y el sensor PixArt PAS106, como la Genius VideoCAM GE111). 

El objetivo principal de este proyecto es interactuar directamente con el hardware de la cámara usando la API de **WinUSB** desde **PowerShell**, evitando la necesidad de instalar controladores privativos antiguos o no compatibles con versiones modernas de Windows. Además, cuenta con un componente en **C++** para procesar y visualizar (renderizar) el video en tiempo real.

A continuación se detalla el análisis archivo por archivo:

## 1. Scripts de Control e Interacción USB (PowerShell)

Estos scripts utilizan `PInvoke` (código en C# incrustado en PowerShell) para llamar a funciones nativas de Windows (`kernel32.dll` y `winusb.dll`), lo que les permite enviar paquetes de control directamente al puerto USB.

### `encender_camara.ps1`
- **¿Qué hace?**: Es el script de inicialización primaria ("Wake-up").
- **Funcionamiento**: 
  1. Busca el dispositivo en el bus PnP de Windows a través de su Hardware ID (`VID_0C45&PID_60B0`).
  2. Genera una ruta simbólica para conectarse por WinUSB.
  3. Envía una señal de control (Control Transfer) al registro `0x01` del chip Sonix para despertar el oscilador (clock) del sensor CMOS, validando que el hardware acepta la estructura de datos.

### `inicializar_sensor_sif.ps1`
- **¿Qué hace?**: Se encarga de la inicialización de bajo nivel de la matriz fotográfica (PixArt PAS106).
- **Funcionamiento**: 
  1. Utiliza el bus SIF (Serial Interface) a través del chip puente (Sonix) para hablar con el sensor de la cámara.
  2. Implementa retardos galvánicos necesarios para la estabilización eléctrica de los cristales.
  3. Inyecta una **"matriz de configuración"** de ingeniería registro por registro, que define cosas como: modo del reloj interno, polaridad, supresión de ruido térmico, ganancia, tiempo de exposición, modo progresivo (anti-flicker) y el encendido de la exposición.
  4. Configura el puente Sonix para capturar en resolución CIF (352x288).

### `capturar_video.ps1`
- **¿Qué hace?**: Configura el dispositivo para transmisión continua y guarda los datos de video crudos.
- **Funcionamiento**: 
  1. Realiza pasos de inicialización similares al script anterior.
  2. Cambia la interfaz USB al "Alternate Setting 1", que es el que habilita el ancho de banda isócrono.
  3. Comienza a leer paquetes isócronos de 128 bytes (el flujo nativo de datos de video).
  4. Toda la data cruda capturada se vuelca progresivamente a un archivo local llamado `stream_crudo.raw` hasta que el usuario detiene el proceso (CTRL+C).

### `analizar_raw.ps1`
- **¿Qué hace?**: Es una herramienta de ingeniería inversa y telemetría de datos.
- **Funcionamiento**: 
  1. Lee el archivo `stream_crudo.raw` generado por la captura.
  2. Calcula estadísticas de los bytes (porcentajes de `0x00` y `0xFF`, y los valores más frecuentes).
  3. Busca "Magic Bytes" o secuencias de sincronización específicas (como cabeceras JPEG `FF D8`, o bytes de sincronización del chip Sonix `AA 00`).
  4. Compara bloques de datos para verificar patrones. Esto es vital para entender cómo decodificar la imagen si el formato no es estándar.

## 2. Aplicación de Visualización (C++ y SDL2)

Estos archivos conforman la herramienta de reproducción, encargada de tomar los bytes crudos y convertirlos en una imagen visible en pantalla.

### `reproductor.cpp`
- **¿Qué hace?**: Es el renderizador principal en tiempo real.
- **Funcionamiento**: 
  1. Levanta un servidor IPC (Comunicación entre Procesos) usando un **Named Pipe** de Windows (`\\.\pipe\GeniusLookVideoPipe`). Esto permite que el script de PowerShell le envíe datos directamente a la memoria sin usar el disco duro.
  2. Inicializa una ventana gráfica utilizando la librería **SDL2** (configurada a 640x480 en formato BGR24).
  3. Tiene un bucle no bloqueante que lee pedazos (chunks) de datos del pipe, reconstruye los frames de la imagen (hasta alcanzar el tamaño completo del frame) y los envía a la tarjeta gráfica (VRAM) para mostrarlos en pantalla. También imprime telemetría en la consola (MB/s y FPS).

### `VideoPlayer.cpp`
- **¿Qué hace?**: Es un archivo de prueba o prototipo estático del renderizador gráfico.
- **Funcionamiento**: 
  1. Inicializa la ventana de SDL2 igual que `reproductor.cpp`.
  2. En lugar de leer de un Named Pipe real, tiene un bucle simulado que genera un color estático falso (llenando un búfer con un tono) para comprobar que la lógica de renderizado en SDL2 funciona a ~60 FPS antes de conectarlo con la ingesta real del USB.

## 3. Scripts de Utilidad (Compilación e Instalación)

### `nstalar_dependencias.ps1`
- **¿Qué hace?**: Automatiza la descarga de las librerías necesarias.
- **Funcionamiento**: 
  Descarga la versión para desarrolladores de SDL2 (`SDL2-devel-2.30.3-mingw.zip`) desde su GitHub oficial, la extrae, busca la versión MinGW de 64 bits (`x86_64-w64-mingw32`) y la organiza en una carpeta local llamada `SDL2`. Limpia los archivos ZIP al terminar.

### `compilar_reproductor.ps1`
- **¿Qué hace?**: Compila el código fuente en C++.
- **Funcionamiento**: 
  Llama al compilador `g++` (MinGW) con los flags de optimización adecuados (`-O3`), incluye las rutas de cabeceras (`include`) y librerías (`lib`) de SDL2 previamente descargadas, y genera el archivo ejecutable `reproductor.exe`. También copia la DLL (`SDL2.dll`) requerida junto al ejecutable para que funcione correctamente.

### `CMakeLists.txt`
- **¿Qué hace?**: Sistema de compilación moderno (alternativo al script de compilación).
- **Funcionamiento**: Contiene las instrucciones estándar de CMake por si el desarrollador prefiere compilar el código C++ en un entorno integrado (IDE como CLion, Visual Studio) en vez del script de PowerShell.

---

## Conclusión de la Auditoría

Este es un proyecto fascinante de **hacking de hardware y desarrollo de drivers de usuario**. Se ha construido un "driver" completamente de cero separando la lógica en dos procesos:
1. **El motor de captura** (PowerShell), que habla con el hardware, despierta los componentes eléctricos (puente Sonix + sensor PixArt) e ingiere el video crudo usando WinUSB.
2. **El motor de presentación** (C++), que recibe esos bytes mediante tuberías de memoria y los dibuja en pantalla usando aceleración gráfica por hardware.
