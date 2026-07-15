#pragma once
#include <vector>
#include <cstdint>
#include <cstring> // Para std::memcpy
#include <span>    // Para std::span

// Alineación estricta a 1 byte para mapeo directo a memoria de hardware
namespace hw_usb {
#pragma pack(push, 1)

struct USB_CONFIGURATION_DESCRIPTOR {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
};

struct USB_INTERFACE_DESCRIPTOR {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
};

struct USB_ENDPOINT_DESCRIPTOR {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
};

#pragma pack(pop)
} // namespace hw_usb

// ... dentro de usb_descriptors.hpp, después del namespace anterior ...

class DescriptorParser {
public:
    struct InterfaceNode {
        hw_usb::USB_INTERFACE_DESCRIPTOR descriptor; // Referencia con namespace
        std::vector<hw_usb::USB_ENDPOINT_DESCRIPTOR> endpoints;
    };

    hw_usb::USB_CONFIGURATION_DESCRIPTOR config_header{}; // Referencia con namespace
    std::vector<InterfaceNode> interfaces;

    void parse(const std::vector<uint8_t>& buffer) {
        std::span<const uint8_t> data{buffer};
        size_t offset = 0;
        interfaces.clear();

        while (offset + 2 <= data.size()) {
            uint8_t length = data[offset];
            uint8_t type = data[offset + 1];
            if (length == 0 || offset + length > data.size()) break;

            switch (type) {
                case 0x02:
                    if (length >= sizeof(hw_usb::USB_CONFIGURATION_DESCRIPTOR)) {
                        std::memcpy(&config_header, &data[offset], sizeof(hw_usb::USB_CONFIGURATION_DESCRIPTOR));
                    }
                    break;
                case 0x04:
                    {
                        hw_usb::USB_INTERFACE_DESCRIPTOR iface;
                        std::memcpy(&iface, &data[offset], sizeof(hw_usb::USB_INTERFACE_DESCRIPTOR));
                        interfaces.push_back({iface, {}}); 
                    }
                    break;
                case 0x05:
                    if (!interfaces.empty()) {
                        hw_usb::USB_ENDPOINT_DESCRIPTOR ep;
                        std::memcpy(&ep, &data[offset], sizeof(hw_usb::USB_ENDPOINT_DESCRIPTOR));
                        interfaces.back().endpoints.push_back(ep);
                    }
                    break;
            }
            offset += length;
        }
    }
};