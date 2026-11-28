#include "texture_translation.h"
#include "prs_decompressor.h"
#include "gvr_parser.h"

#include <android/log.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <iterator>
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
    uint8_t pixelFormat;
    uint8_t dataFormat;
    size_t pixelDataOffset;
    std::vector<uint8_t> pixelData;
};

static uint16_t readBigEndian16(const uint8_t* data) {
    return static_cast<uint16_t>(
            (static_cast<uint16_t>(data[0]) << 8u) | data[1]);
}

bool readPVRDimensions(
        const std::vector<uint8_t>& textureData,
        uint32_t& width,
        uint32_t& height,
        uint8_t& pixelFormat,
        uint8_t& dataFormat,
        uint8_t& textureFlags,
        bool& isGvr,
        size_t& pixelDataOffset) {
    width = 0;
    height = 0;
    pixelFormat = 0;
    dataFormat = 0;
    textureFlags = 0;
    isGvr = false;
    pixelDataOffset = 0;

    for (size_t offset = 0; offset + 16 <= textureData.size(); ++offset) {
        const bool isPVRT = std::memcmp(
                textureData.data() + offset, "PVRT", 4) == 0;
        const bool foundGvrt = std::memcmp(
                textureData.data() + offset, "GVRT", 4) == 0;
        if (!isPVRT && !foundGvrt) continue;

        const uint8_t* header = textureData.data() + offset;
        const uint16_t parsedWidth = readBigEndian16(header + 12);
        const uint16_t parsedHeight = readBigEndian16(header + 14);
        if (parsedWidth == 0 || parsedHeight == 0) continue;

        width = parsedWidth;
        height = parsedHeight;
        isGvr = foundGvrt;
        if (isGvr) {
            // GVRT +0x0A guarda flags; +0x0B guarda o formato dos dados.
            textureFlags = header[10];
            pixelFormat = static_cast<uint8_t>((textureFlags >> 4u) & 0x0Fu);
            dataFormat = header[11];
        } else {
            // PVRT usa formato de pixel em +0x08 e formato de dados em +0x09.
            pixelFormat = header[8];
            dataFormat = header[9];
        }
        pixelDataOffset = offset + 16;
        return true;
    }

    return false;
}



TextureInfo parsePVRTexture(const std::string& filePath) {
    TextureInfo tex{0, 0, 0, 0, 0, 0, {}};

    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PVM_PARSER_LOG_TAG,
                "Erro ao abrir textura: %s",
                filePath.c_str());
        return tex;
    }

    const std::vector<uint8_t> data(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());

    uint8_t textureFlags = 0;
    bool isGvr = false;
    if (readPVRDimensions(
                data,
                tex.width,
                tex.height,
                tex.pixelFormat,
                tex.dataFormat,
                textureFlags,
                isGvr,
                tex.pixelDataOffset)) {
        __android_log_print(
                ANDROID_LOG_INFO,
                PVM_PARSER_LOG_TAG,
                "PVR/GVR lido com sucesso. Dimensões: %u x %u, formato pixel=%u dados=%u",
                tex.width,
                tex.height,
                tex.pixelFormat,
                tex.dataFormat);
    }

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

        if (!salvarTGA(dstFilePath, cleanRgbaData, width, height)) continue;

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
        const std::string dstFilePath = finalTexPath + "/" + baseName + ".tga";

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

        if (salvarTGA(dstFilePath, cleanRgbaData, width, height)) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    SA2B_FIX_LOG_TAG,
                    "Traduzido e salvo: %s.tga",
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

void processTexturesWithChecks(
        const std::string& baseDirPath,
        const std::function<void()>& onFileTranslated) {
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

        const std::string extension = getFileExtension(fileName);
        if (extension == ".pvm" || extension == ".gvm") {
            // PVM/GVM são containers. Eles precisam ser abertos pelo parser
            // do container; nunca devem ser interpretados como pixels crus.
            __android_log_print(
                    ANDROID_LOG_INFO,
                    PIPELINE_LOG_TAG,
                    "Container de texturas aguardando extração: %s",
                    fileName.c_str());
            continue;
        }

        const size_t lastDot = fileName.find_last_of('.');
        const std::string baseName = lastDot == std::string::npos
                ? fileName
                : fileName.substr(0, lastDot);
        const std::string srcFilePath = tempTextPath + "/" + fileName;
        const std::string dstFilePath = finalTexPath + "/" + baseName + ".tga";

        if (fileExists(dstFilePath)) {
            std::remove(srcFilePath.c_str());
            if (onFileTranslated) onFileTranslated();
            continue;
        }

        if (extension == ".tga") {
            const std::vector<uint8_t> tgaData = lerArquivoBinario(srcFilePath);
            if (tgaData.empty()) continue;
            std::ofstream output(dstFilePath, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) continue;
            output.write(reinterpret_cast<const char*>(tgaData.data()),
                    static_cast<std::streamsize>(tgaData.size()));
            output.close();
            if (!output.good()) continue;
            std::remove(srcFilePath.c_str());
            if (onFileTranslated) onFileTranslated();
            continue;
        }

        const std::vector<uint8_t> sourceData =
                lerArquivoBinario(srcFilePath);
        if (sourceData.empty()) continue;

        uint32_t parsedWidth = 256;
        uint32_t parsedHeight = 256;
        uint8_t pixelFormat = 0;
        uint8_t dataFormat = 0;
        uint8_t textureFlags = 0;
        bool isGvr = false;
        size_t pixelDataOffset = 0;
        const bool hasPvrHeader = readPVRDimensions(
                sourceData,
                parsedWidth,
                parsedHeight,
                pixelFormat,
                dataFormat,
                textureFlags,
                isGvr,
                pixelDataOffset);

        if (hasPvrHeader) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    PIPELINE_LOG_TAG,
                    "Cabeçalho %s: %ux%u, GVR=%d, flags=0x%02X, pixel/paleta=%u, formato=%u",
                    fileName.c_str(),
                    parsedWidth,
                    parsedHeight,
                    isGvr ? 1 : 0,
                    textureFlags,
                    pixelFormat,
                    dataFormat);
        }

        if (extension == ".gvr" || (hasPvrHeader && isGvr)) {
            GvrTexture gvr;
            std::string parseError;
            if (!parseGvrTexture(sourceData, gvr, parseError)) {
                __android_log_print(
                        ANDROID_LOG_WARN,
                        PIPELINE_LOG_TAG,
                        "Falha ao interpretar %s: %s",
                        fileName.c_str(),
                        parseError.c_str());
                continue;
            }

            if (salvarTGA(dstFilePath, gvr.rgba, gvr.width, gvr.height)) {
                __android_log_print(
                        ANDROID_LOG_INFO,
                        PIPELINE_LOG_TAG,
                        "GVR convertido para TGA: %s (%ux%u, formato=%u)",
                        baseName.c_str(),
                        gvr.width,
                        gvr.height,
                        gvr.dataFormat);
                std::remove(srcFilePath.c_str());
                if (onFileTranslated) onFileTranslated();
            }
            continue;
        }

        if (parsedWidth == 0 || parsedHeight == 0
                || parsedWidth > static_cast<uint32_t>(std::numeric_limits<int>::max())
                || parsedHeight > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
            __android_log_print(
                    ANDROID_LOG_WARN,
                    PIPELINE_LOG_TAG,
                    "Dimensões inválidas para converter: %s",
                    fileName.c_str());
            continue;
        }

        const size_t rawSize = static_cast<size_t>(parsedWidth)
                * static_cast<size_t>(parsedHeight) * 2u;
        if (pixelDataOffset > sourceData.size()
                || rawSize > sourceData.size() - pixelDataOffset) {
            __android_log_print(
                    ANDROID_LOG_WARN,
                    PIPELINE_LOG_TAG,
                    "Dados insuficientes para %ux%u: %s",
                    parsedWidth,
                    parsedHeight,
                    fileName.c_str());
            continue;
        }

        const std::vector<uint8_t> cleanRgbaData = convertToRGBA8888(
                sourceData.data() + pixelDataOffset,
                static_cast<int>(parsedWidth),
                static_cast<int>(parsedHeight),
                pixelFormat);
        if (cleanRgbaData.empty()) continue;

        if (salvarTGA(dstFilePath, cleanRgbaData, parsedWidth, parsedHeight)) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    PIPELINE_LOG_TAG,
                    "Sucesso! Traduzido e salvo: %s.tga",
                    baseName.c_str());

            if (std::remove(srcFilePath.c_str()) == 0) {
                __android_log_print(
                        ANDROID_LOG_DEBUG,
                        PIPELINE_LOG_TAG,
                        "Original removido da temp: %s",
                        fileName.c_str());
            }
            if (onFileTranslated) onFileTranslated();
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
        const std::string dstFilePath = finalTexPath + "/" + baseName + ".tga";

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

        if (salvarTGA(dstFilePath, cleanRgbaData, width, height)) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    CLEAN_PIPELINE_LOG_TAG,
                    "Traduzido e salvo na pasta final: %s.tga",
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
// Grava RGBA8888 como TGA 32-bit não comprimido, sem biblioteca externa.
bool salvarTGA(
        const std::string& caminhoSaida,
        const std::vector<uint8_t>& dadosPixels,
        uint32_t largura,
        uint32_t altura) {
    if (largura == 0 || altura == 0
            || largura > 65535u || altura > 65535u
            || static_cast<size_t>(largura)
                    > std::numeric_limits<size_t>::max()
                            / static_cast<size_t>(altura) / 4u) {
        return false;
    }

    const size_t tamanhoEsperado =
            static_cast<size_t>(largura) * altura * 4u;
    if (dadosPixels.size() < tamanhoEsperado) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "Dados RGBA insuficientes para TGA %ux%u: esperado %zu, recebido %zu bytes.",
                largura,
                altura,
                tamanhoEsperado,
                dadosPixels.size());
        return false;
    }

    uint8_t header[18] = {};
    header[2] = 2;
    header[12] = static_cast<uint8_t>(largura & 0xFFu);
    header[13] = static_cast<uint8_t>((largura >> 8u) & 0xFFu);
    header[14] = static_cast<uint8_t>(altura & 0xFFu);
    header[15] = static_cast<uint8_t>((altura >> 8u) & 0xFFu);
    header[16] = 32;
    header[17] = 0x28;

    std::vector<uint8_t> bgra(tamanhoEsperado);
    for (size_t i = 0; i < tamanhoEsperado; i += 4u) {
        bgra[i] = dadosPixels[i + 2u];
        bgra[i + 1u] = dadosPixels[i + 1u];
        bgra[i + 2u] = dadosPixels[i];
        bgra[i + 3u] = dadosPixels[i + 3u];
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
            reinterpret_cast<const char*>(header),
            static_cast<std::streamsize>(sizeof(header)));
    arquivoSaida.write(
            reinterpret_cast<const char*>(bgra.data()),
            static_cast<std::streamsize>(bgra.size()));
    arquivoSaida.close();
    return arquivoSaida.good();
}

