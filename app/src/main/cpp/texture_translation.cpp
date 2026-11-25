#include "texture_translation.h"
#include "prs_decompressor.h"

#include <android/log.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <limits>
#include <sys/stat.h>
#include <vector>

#define TEXTURE_TRANSLATOR_LOG_TAG "SA2B_TEXTURE_TRANSLATOR"
#define PVM_PARSER_LOG_TAG "SA2B_PVM_PARSER"

struct TextureInfo {
    uint32_t globalIndex;
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> pixelData;
};
TextureInfo parsePVRTexture(const std::string& filePath) {
    TextureInfo tex{0, 0, 0, {}};

    FILE* file = std::fopen(filePath.c_str(), "rb");
    if (!file) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PVM_PARSER_LOG_TAG,
                "Erro ao abrir textura: %s",
                filePath.c_str());
        return tex;
    }

    char magicGBIX[4];
    if (std::fread(magicGBIX, 1, sizeof(magicGBIX), file) == sizeof(magicGBIX)
            && std::string(magicGBIX, sizeof(magicGBIX)) == "GBIX") {
        uint32_t gbixSize = 0;
        uint32_t globalIndex = 0;
        std::fread(&gbixSize, sizeof(gbixSize), 1, file);
        std::fread(&globalIndex, sizeof(globalIndex), 1, file);

        tex.globalIndex = globalIndex;
        __android_log_print(
                ANDROID_LOG_INFO,
                PVM_PARSER_LOG_TAG,
                "GBIX encontrado! Global Index: %u",
                globalIndex);
    } else {
        std::rewind(file);
    }

    char magicPVRT[4];
    if (std::fread(magicPVRT, 1, sizeof(magicPVRT), file) == sizeof(magicPVRT)
            && std::string(magicPVRT, sizeof(magicPVRT)) == "PVRT") {
        uint32_t pvrtSize = 0;
        std::fread(&pvrtSize, sizeof(pvrtSize), 1, file);

        uint8_t pvrHeader[16] = {};
        if (std::fread(pvrHeader, 1, sizeof(pvrHeader), file) == sizeof(pvrHeader)) {
            tex.width = pvrHeader[4];
            tex.height = pvrHeader[5];

            __android_log_print(
                    ANDROID_LOG_INFO,
                    PVM_PARSER_LOG_TAG,
                    "PVR lido com sucesso. Dimensões: %u x %u",
                    tex.width,
                    tex.height);
        }
    }

    std::fclose(file);
    return tex;
}

std::vector<uint8_t> convertToRGBA8888(
        const uint8_t* srcData,
        int width,
        int height,
        int /* format */) {
    if (srcData == nullptr || width <= 0 || height <= 0) {
        return {};
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> rgbaPixels(pixelCount * 4);
    const uint16_t* src16 = reinterpret_cast<const uint16_t*>(srcData);

    for (size_t i = 0; i < pixelCount; ++i) {
        const uint16_t pixel = src16[i];
        rgbaPixels[i * 4] = static_cast<uint8_t>(((pixel >> 8) & 0x0F) * 17);
        rgbaPixels[i * 4 + 1] = static_cast<uint8_t>(((pixel >> 4) & 0x0F) * 17);
        rgbaPixels[i * 4 + 2] = static_cast<uint8_t>((pixel & 0x0F) * 17);
        rgbaPixels[i * 4 + 3] = static_cast<uint8_t>(((pixel >> 12) & 0x0F) * 17);
    }

    return rgbaPixels;
}

void translateAndSaveTextures(const std::string& baseDirPath) {
    const std::string tempTextPath = baseDirPath + "/temp/textures";
    const std::string finalTexPath = baseDirPath + "/textures";

    DIR* dir = opendir(tempTextPath.c_str());
    if (!dir) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                TEXTURE_TRANSLATOR_LOG_TAG,
                "Não foi possível abrir a pasta temp de texturas.");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string fileName = entry->d_name;
        if (fileName == "." || fileName == "..") continue;

        const std::string srcFilePath = tempTextPath + "/" + fileName;
        const std::string dstFilePath = finalTexPath + "/" + fileName;
        FILE* inFile = std::fopen(srcFilePath.c_str(), "rb");
        if (!inFile) continue;

        const int width = 256;
        const int height = 256;
        const size_t rawSize = static_cast<size_t>(width) * height * 2;
        std::vector<uint8_t> rawPixelData(rawSize);
        const size_t bytesRead = std::fread(rawPixelData.data(), 1, rawSize, inFile);
        std::fclose(inFile);

        if (bytesRead < rawSize) {
            __android_log_print(
                    ANDROID_LOG_WARN,
                    TEXTURE_TRANSLATOR_LOG_TAG,
                    "Dados insuficientes para converter: %s",
                    fileName.c_str());
            continue;
        }

        const std::vector<uint8_t> cleanRgbaData =
                convertToRGBA8888(rawPixelData.data(), width, height, 1);

        FILE* outFile = std::fopen(dstFilePath.c_str(), "wb");
        if (!outFile) continue;
        std::fwrite(cleanRgbaData.data(), 1, cleanRgbaData.size(), outFile);
        std::fclose(outFile);

        __android_log_print(
                ANDROID_LOG_INFO,
                TEXTURE_TRANSLATOR_LOG_TAG,
                "Textura traduzida e salva: %s",
                fileName.c_str());
    }

    closedir(dir);
}

#define SA2B_FIX_LOG_TAG "SA2B_FIX"

void translateAndSaveTexturesFixed(const std::string& baseDirPath) {
    const std::string tempTextPath = baseDirPath + "/temp/textures";
    const std::string finalTexPath = baseDirPath + "/textures";

    DIR* dir = opendir(tempTextPath.c_str());
    if (!dir) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                SA2B_FIX_LOG_TAG,
                "Pasta temp/textures não encontrada.");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string fileName = entry->d_name;
        if (fileName == "." || fileName == "..") continue;

        const size_t lastDot = fileName.find_last_of('.');
        const std::string baseName = lastDot == std::string::npos
                ? fileName
                : fileName.substr(0, lastDot);

        const std::string srcFilePath = tempTextPath + "/" + fileName;
        const std::string dstFilePath = finalTexPath + "/" + baseName + ".rgba";

        FILE* inFile = std::fopen(srcFilePath.c_str(), "rb");
        if (!inFile) continue;

        const int width = 256;
        const int height = 256;
        const size_t rawSize = static_cast<size_t>(width) * height * 2;
        std::vector<uint8_t> rawData(rawSize);
        const size_t bytesRead = std::fread(rawData.data(), 1, rawData.size(), inFile);
        std::fclose(inFile);

        if (bytesRead != rawData.size()) {
            __android_log_print(
                    ANDROID_LOG_WARN,
                    SA2B_FIX_LOG_TAG,
                    "Dados insuficientes para converter: %s",
                    fileName.c_str());
            continue;
        }

        const std::vector<uint8_t> cleanRgbaData =
                convertToRGBA8888(rawData.data(), width, height, 1);

        FILE* outFile = std::fopen(dstFilePath.c_str(), "wb");
        if (!outFile) continue;
        const size_t bytesWritten = std::fwrite(
                cleanRgbaData.data(), 1, cleanRgbaData.size(), outFile);
        std::fclose(outFile);

        if (bytesWritten == cleanRgbaData.size()) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    SA2B_FIX_LOG_TAG,
                    "Traduzido e salvo: %s.rgba",
                    baseName.c_str());
        }
    }

    closedir(dir);
}

#define CLEAN_PIPELINE_LOG_TAG "SA2B_CLEAN_PIPELINE"
#define PIPELINE_LOG_TAG "SA2B_PIPELINE"

bool fileExists(const std::string& path) {
    struct stat fileStatus;
    return stat(path.c_str(), &fileStatus) == 0;
}

void processTexturesWithChecks(const std::string& baseDirPath) {
    const std::string tempTextPath = baseDirPath + "/temp/textures";
    const std::string finalTexPath = baseDirPath + "/textures";

    DIR* dir = opendir(tempTextPath.c_str());
    if (!dir) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PIPELINE_LOG_TAG,
                "Pasta temp/textures não encontrada.");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string fileName = entry->d_name;
        if (fileName == "." || fileName == "..") continue;

        const size_t lastDot = fileName.find_last_of('.');
        const std::string baseName = lastDot == std::string::npos
                ? fileName
                : fileName.substr(0, lastDot);
        const std::string srcFilePath = tempTextPath + "/" + fileName;
        const std::string dstFilePath = finalTexPath + "/" + baseName + ".rgba";

        if (fileExists(dstFilePath)) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    PIPELINE_LOG_TAG,
                    "Já traduzido anteriormente. Ignorando e limpando temp: %s",
                    fileName.c_str());
            std::remove(srcFilePath.c_str());
            continue;
        }

        FILE* inFile = std::fopen(srcFilePath.c_str(), "rb");
        if (!inFile) continue;

        const int width = 256;
        const int height = 256;
        const size_t rawSize = static_cast<size_t>(width) * height * 2;
        std::vector<uint8_t> rawData(rawSize);
        const size_t bytesRead = std::fread(rawData.data(), 1, rawData.size(), inFile);
        std::fclose(inFile);

        if (bytesRead != rawData.size()) {
            __android_log_print(
                    ANDROID_LOG_WARN,
                    PIPELINE_LOG_TAG,
                    "Dados insuficientes para converter: %s",
                    fileName.c_str());
            continue;
        }

        const std::vector<uint8_t> cleanRgbaData =
                convertToRGBA8888(rawData.data(), width, height, 1);
        if (cleanRgbaData.empty()) continue;

        FILE* outFile = std::fopen(dstFilePath.c_str(), "wb");
        if (!outFile) continue;

        const size_t bytesWritten = std::fwrite(
                cleanRgbaData.data(), 1, cleanRgbaData.size(), outFile);
        const bool outputClosed = std::fclose(outFile) == 0;

        if (bytesWritten == cleanRgbaData.size() && outputClosed) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    PIPELINE_LOG_TAG,
                    "Sucesso! Traduzido e salvo: %s.rgba",
                    baseName.c_str());

            if (std::remove(srcFilePath.c_str()) == 0) {
                __android_log_print(
                        ANDROID_LOG_DEBUG,
                        PIPELINE_LOG_TAG,
                        "Original removido da temp: %s",
                        fileName.c_str());
            }
        }
    }

    closedir(dir);
}


void translateAndCleanTextures(const std::string& baseDirPath) {
    const std::string tempTextPath = baseDirPath + "/temp/textures";
    const std::string finalTexPath = baseDirPath + "/textures";

    DIR* dir = opendir(tempTextPath.c_str());
    if (!dir) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                CLEAN_PIPELINE_LOG_TAG,
                "Pasta temp/textures não encontrada.");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string fileName = entry->d_name;
        if (fileName == "." || fileName == "..") continue;

        const size_t lastDot = fileName.find_last_of('.');
        const std::string baseName = lastDot == std::string::npos
                ? fileName
                : fileName.substr(0, lastDot);
        const std::string srcFilePath = tempTextPath + "/" + fileName;
        const std::string dstFilePath = finalTexPath + "/" + baseName + ".rgba";

        FILE* inFile = std::fopen(srcFilePath.c_str(), "rb");
        if (!inFile) continue;

        const int width = 256;
        const int height = 256;
        const size_t rawSize = static_cast<size_t>(width) * height * 2;
        std::vector<uint8_t> rawData(rawSize);
        const size_t bytesRead = std::fread(rawData.data(), 1, rawData.size(), inFile);
        std::fclose(inFile);

        if (bytesRead != rawSize) {
            __android_log_print(
                    ANDROID_LOG_WARN,
                    CLEAN_PIPELINE_LOG_TAG,
                    "Dados insuficientes para converter: %s",
                    fileName.c_str());
            continue;
        }

        const std::vector<uint8_t> cleanRgbaData =
                convertToRGBA8888(rawData.data(), width, height, 1);
        if (cleanRgbaData.empty()) continue;

        FILE* outFile = std::fopen(dstFilePath.c_str(), "wb");
        if (!outFile) continue;

        const size_t bytesWritten = std::fwrite(
                cleanRgbaData.data(), 1, cleanRgbaData.size(), outFile);
        const bool outputClosed = std::fclose(outFile) == 0;

        if (bytesWritten == cleanRgbaData.size() && outputClosed) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    CLEAN_PIPELINE_LOG_TAG,
                    "Traduzido e salvo na pasta final: %s.rgba",
                    baseName.c_str());

            if (std::remove(srcFilePath.c_str()) == 0) {
                __android_log_print(
                        ANDROID_LOG_DEBUG,
                        CLEAN_PIPELINE_LOG_TAG,
                        "Arquivo bruto removido da temp: %s",
                        fileName.c_str());
            }
        }
    }

    closedir(dir);
}

void translateTextures(const std::string& baseDirPath) {
    std::string tempTextPath = baseDirPath + "/temp/textures";
    std::string finalTexPath = baseDirPath + "/textures";

    DIR* dir = opendir(tempTextPath.c_str());
    if (!dir) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                TEXTURE_TRANSLATOR_LOG_TAG,
                "Não foi possível abrir a pasta temp de texturas.");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fileName = entry->d_name;
        if (fileName == "." || fileName == "..") continue;

        std::string srcFilePath = tempTextPath + "/" + fileName;
        std::string dstFilePath = finalTexPath + "/" + fileName;

        FILE* inFile = std::fopen(srcFilePath.c_str(), "rb");
        if (!inFile) continue;

        // TODO: decodificar GBIX/PVM e converter os pixels para RGBA8888.
        FILE* outFile = std::fopen(dstFilePath.c_str(), "wb");
        if (outFile) {
            std::vector<uint8_t> buffer;
            std::fseek(inFile, 0, SEEK_END);
            long size = std::ftell(inFile);
            std::rewind(inFile);
            if (size > 0) {
                buffer.resize(static_cast<size_t>(size));
                std::fread(buffer.data(), 1, buffer.size(), inFile);
                std::fwrite(buffer.data(), 1, buffer.size(), outFile);
            }
            std::fclose(outFile);
        }

        std::fclose(inFile);
        __android_log_print(
                ANDROID_LOG_INFO,
                TEXTURE_TRANSLATOR_LOG_TAG,
                "Textura traduzida: %s",
                fileName.c_str());
    }

    closedir(dir);
}

#define PRS_PIPELINE_LOG_TAG "SA2B_PIPELINE_PRS"
// Salva bytes RGBA crus em modo binário após validar dimensões e tamanho.
bool salvarRGBA(
        const std::string& caminhoSaida,
        const std::vector<uint8_t>& dadosPixels,
        uint32_t largura,
        uint32_t altura) {
    if (largura == 0 || altura == 0
            || static_cast<size_t>(largura)
                    > std::numeric_limits<size_t>::max()
                            / static_cast<size_t>(altura) / 4) {
        return false;
    }

    const size_t tamanhoEsperado =
            static_cast<size_t>(largura) * altura * 4;
    if (dadosPixels.size() < tamanhoEsperado) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "Dados RGBA insuficientes para %ux%u: esperado %zu, recebido %zu bytes.",
                largura,
                altura,
                tamanhoEsperado,
                dadosPixels.size());
        return false;
    }

    std::ofstream arquivoSaida(
            caminhoSaida, std::ios::binary | std::ios::trunc);
    if (!arquivoSaida.is_open()) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "Não foi possível criar: %s",
                caminhoSaida.c_str());
        return false;
    }

    arquivoSaida.write(
            reinterpret_cast<const char*>(dadosPixels.data()),
            static_cast<std::streamsize>(tamanhoEsperado));
    arquivoSaida.close();
    return arquivoSaida.good();
}

