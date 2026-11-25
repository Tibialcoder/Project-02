#include "prs_decompressor.h"
#include "resource_file_manager.h"
#include "texture_translation.h"

#include <android/log.h>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <limits>

#define PRS_PIPELINE_LOG_TAG "SA2B_PIPELINE_PRS"

std::string getFileExtension(const std::string& filename) {
    const size_t lastDot = filename.find_last_of('.');
    if (lastDot == std::string::npos) return "";

    std::string extension = filename.substr(lastDot);
    std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

// Descomprime um fluxo PRS (bits de controle LSB-first, cópias curtas e longas).
bool DecompressPRS(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst) {
    constexpr size_t kMaxOutputSize = 256u * 1024u * 1024u;
    dst.clear();
    if (src.empty()) return false;

    size_t inputPos = 0;
    uint8_t controlByte = 0;
    unsigned int controlBitsRemaining = 0;

    auto readBit = [&]() -> int {
        if (controlBitsRemaining == 0) {
            if (inputPos >= src.size()) return -1;
            controlByte = src[inputPos++];
            controlBitsRemaining = 8;
        }
        const int bit = controlByte & 1u;
        controlByte >>= 1u;
        --controlBitsRemaining;
        return bit;
    };

    auto readByte = [&]() -> int {
        if (inputPos >= src.size()) return -1;
        return src[inputPos++];
    };

    while (true) {
        const int literalFlag = readBit();
        if (literalFlag < 0) return false;

        if (literalFlag != 0) {
            const int value = readByte();
            if (value < 0 || dst.size() >= kMaxOutputSize) return false;
            dst.push_back(static_cast<uint8_t>(value));
            continue;
        }

        const int longCopyFlag = readBit();
        if (longCopyFlag < 0) return false;

        size_t copyLength;
        int copyOffset;
        if (longCopyFlag != 0) {
            const int lowByte = readByte();
            const int highByte = readByte();
            if (lowByte < 0 || highByte < 0) return false;

            const uint16_t packedOffset = static_cast<uint16_t>(
                    lowByte | (highByte << 8));
            if (packedOffset == 0) return !dst.empty();

            const uint16_t encodedOffset = packedOffset >> 3u;
            copyOffset = (encodedOffset & 0x1000u) != 0
                    ? static_cast<int>(encodedOffset) - 0x2000
                    : static_cast<int>(encodedOffset);

            const unsigned int lengthCode = packedOffset & 0x7u;
            if (lengthCode == 0) {
                const int extendedLength = readByte();
                if (extendedLength < 0) return false;
                copyLength = static_cast<size_t>(extendedLength) + 1;
            } else {
                copyLength = static_cast<size_t>(lengthCode) + 2;
            }
        } else {
            const int bit0 = readBit();
            const int bit1 = readBit();
            const int offsetByte = readByte();
            if (bit0 < 0 || bit1 < 0 || offsetByte < 0) return false;

            copyLength = static_cast<size_t>((bit0 | (bit1 << 1)) + 2);
            copyOffset = static_cast<int8_t>(static_cast<uint8_t>(offsetByte));
        }

        if (copyOffset >= 0 || static_cast<size_t>(-copyOffset) > dst.size()
                || copyLength > kMaxOutputSize - dst.size()) {
            return false;
        }

        size_t copyPos = dst.size() - static_cast<size_t>(-copyOffset);
        for (size_t i = 0; i < copyLength; ++i) {
            if (copyPos >= dst.size()) return false;
            dst.push_back(dst[copyPos++]);
        }
    }
}

#define PRS_PIPELINE_LOG_TAG "SA2B_PIPELINE_PRS"

// Lê um arquivo completo em modo binário, com limite para evitar alocações excessivas.
std::vector<uint8_t> lerArquivoBinario(const std::string& caminho) {
    std::ifstream arquivo(caminho, std::ios::binary | std::ios::ate);
    if (!arquivo.is_open()) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "Não foi possível abrir o arquivo: %s",
                caminho.c_str());
        return {};
    }

    const std::streamsize tamanho = arquivo.tellg();
    constexpr std::streamsize kMaxInputSize = 256ll * 1024ll * 1024ll;
    if (tamanho <= 0 || tamanho > kMaxInputSize) return {};

    arquivo.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(tamanho));
    if (!arquivo.read(reinterpret_cast<char*>(buffer.data()), tamanho)) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "Falha ao ler os bytes do arquivo: %s",
                caminho.c_str());
        return {};
    }
    return buffer;
}

// Interface de conveniência que reutiliza o decodificador PRS existente.
std::vector<uint8_t> descomprimirPRS(const std::vector<uint8_t>& comprimido) {
    std::vector<uint8_t> saida;
    if (!DecompressPRS(comprimido, saida)) saida.clear();
    return saida;
}

void processPipelineWithDecompressed(const std::string& baseDirPath) {
    const std::string tempPath = baseDirPath + "/temp";
    const std::string decompPath = tempPath + "/decompressed";
    createDecompressedDirectory(baseDirPath);

    const std::vector<std::string> sourceDirs = {
            tempPath,
            tempPath + "/textures",
            tempPath + "/models",
            tempPath + "/audio",
            tempPath + "/ui",
            tempPath + "/misc"
    };

    for (const std::string& sourceDir : sourceDirs) {
        DIR* dir = opendir(sourceDir.c_str());
        if (!dir) continue;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            const std::string fileName = entry->d_name;
            if (fileName == "." || fileName == ".."
                    || getFileExtension(fileName) != ".prs") {
                continue;
            }

            const std::string srcFilePath = sourceDir + "/" + fileName;
            struct stat sourceStatus {};
            if (stat(srcFilePath.c_str(), &sourceStatus) != 0
                    || !S_ISREG(sourceStatus.st_mode)) {
                continue;
            }

            const std::vector<uint8_t> compressedData =
                    lerArquivoBinario(srcFilePath);
            if (compressedData.empty()) continue;

            const std::vector<uint8_t> decompressedData =
                    descomprimirPRS(compressedData);
            if (decompressedData.empty()) {
                __android_log_print(
                        ANDROID_LOG_ERROR,
                        PRS_PIPELINE_LOG_TAG,
                        "Falha ao descompactar PRS: %s",
                        fileName.c_str());
                continue;
            }

            const size_t lastDot = fileName.find_last_of('.');
            const std::string cleanName = fileName.substr(0, lastDot);
            const std::string decompFilePath = decompPath + "/" + cleanName;

            bool writeSucceeded = false;
            if (getFileExtension(cleanName) == ".rgba") {
                // O nome .rgba indica que o payload descompactado já contém pixels RGBA.
                writeSucceeded = salvarRGBA(
                        decompFilePath, decompressedData, 256, 256);
            } else {
                std::ofstream outFile(
                        decompFilePath, std::ios::binary | std::ios::trunc);
                if (!outFile.is_open()) continue;

                outFile.write(
                        reinterpret_cast<const char*>(decompressedData.data()),
                        static_cast<std::streamsize>(decompressedData.size()));
                writeSucceeded = outFile.good();
                outFile.close();
                writeSucceeded = writeSucceeded && outFile.good();
            }
            if (!writeSucceeded) continue;

            __android_log_print(
                    ANDROID_LOG_INFO,
                    PRS_PIPELINE_LOG_TAG,
                    "Descompactado em temp/decompressed: %s",
                    cleanName.c_str());
            if (std::remove(srcFilePath.c_str()) != 0) {
                __android_log_print(
                        ANDROID_LOG_WARN,
                        PRS_PIPELINE_LOG_TAG,
                        "Não foi possível remover o PRS original: %s",
                        fileName.c_str());
            }
        }

        closedir(dir);
    }

    // Arquivos revelados ficam em temp/decompressed para processamento posterior.
}

