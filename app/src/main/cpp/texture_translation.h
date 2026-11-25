#pragma once
#include <cstdint>
#include <string>
#include <vector>

bool salvarRGBA(
        const std::string& caminhoSaida,
        const std::vector<uint8_t>& dadosPixels,
        uint32_t largura,
        uint32_t altura);
void processTexturesWithChecks(const std::string& baseDirPath);
void translateTextures(const std::string& baseDirPath);
