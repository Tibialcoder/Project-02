#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ExtractedTexture {
    std::string name;
    std::vector<uint8_t> pvrData;
};

struct PVMTextureMetadata {
    std::string name;
    uint32_t offset;
};

bool ExtractPVMContainer(
        const std::vector<uint8_t>& pvmFileData,
        std::vector<ExtractedTexture>& outTextures);

bool ExtractPVMContainer(
        const std::vector<uint8_t>& pvmFileData,
        std::vector<ExtractedTexture>& outTextures,
        const std::string& nomePVM,
        const std::filesystem::path& pastaDestino);

bool SalvarPVMEmJSON(
        const std::string& nomePVM,
        const std::vector<PVMTextureMetadata>& entriesList,
        const std::filesystem::path& pastaDestino);
