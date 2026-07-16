#pragma once
#include <windows.h>
#include <winusb.h>
#include <usb100.h>
#include <string>

namespace VideoCapture {

    // Activa el DMA del chip SN9C103 (Stream On)
    bool StartStreaming(WINUSB_INTERFACE_HANDLE handle);

    // Lectura isócrona simple de diagnóstico (una sola transferencia)
    bool TestStream(WINUSB_INTERFACE_HANDLE handle, UCHAR endpointId);

    // Captura frames completos del sensor en loop continuo y guarda al disco
    // Devuelve la cantidad de frames guardados
    int CaptureFrames(WINUSB_INTERFACE_HANDLE handle, UCHAR endpointId,
                      int numFrames, const std::string& outputDir);

}
