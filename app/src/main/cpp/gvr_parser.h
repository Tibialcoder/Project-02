#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct GvrTexture {
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t flags = 0;
    uint8_t pixelFormat = 0;
    uint8_t dataFormat = 0;
    std::vector<uint8_t> rgba;
};

// Decodifica os formatos GameCube GVR não indexados. Formatos indexados precisam de paleta.
bool parseGvrTexture(
        const std::vector<uint8_t>& fileData,
        GvrTexture& texture,
        std::string& error);
