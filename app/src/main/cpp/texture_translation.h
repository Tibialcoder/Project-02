#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

bool readPVRDimensions(
        const std::vector<uint8_t>& textureData,
        uint32_t& width,
        uint32_t& height,
        uint8_t& pixelFormat,
        uint8_t& dataFormat,
        uint8_t& textureFlags,
        bool& isGvr,
        size_t& pixelDataOffset);

bool salvarTGA(
        const std::string& caminhoSaida,
        const std::vector<uint8_t>& dadosPixels,
        uint32_t largura,
        uint32_t altura);
void processTexturesWithChecks(
        const std::string& baseDirPath,
        const std::function<void()>& onFileTranslated = {});
void translateTextures(const std::string& baseDirPath);
