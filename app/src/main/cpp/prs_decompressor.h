#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

bool DecompressPRS(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst);
std::vector<uint8_t> lerArquivoBinario(const std::string& caminho);
std::vector<uint8_t> descomprimirPRS(const std::vector<uint8_t>& comprimido);
std::string getFileExtension(const std::string& filename);
std::string inferResourceExtension(const std::vector<uint8_t>& data);
void processPipelineWithDecompressed(
        const std::string& baseDirPath,
        const std::function<void()>& onFileTranslated = {});
