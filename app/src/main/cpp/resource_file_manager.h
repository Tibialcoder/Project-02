#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

void createDecompressedDirectory(const std::string& baseDirPath);
bool createDirectory(const std::string& path);
std::string classifyFile(const std::string& filename);
bool extrairParaTemp(
        const std::vector<uint8_t>& dados,
        const std::filesystem::path& caminhoTemp);
bool moverParaPastaDefinitiva(
        const std::filesystem::path& caminhoTemp,
        const std::filesystem::path& caminhoDestinoFinal);
