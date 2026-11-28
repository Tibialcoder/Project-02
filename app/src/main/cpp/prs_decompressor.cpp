#include "prs_decompressor.h"
#include "resource_file_manager.h"
#include "texture_translation.h"

#include <android/log.h>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <future>
#include <sys/stat.h>
#include <thread>
#include <algorithm>
#include <cctype>
#include <cstring>
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

std::string inferResourceExtension(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return ".bin";

    const char* magic = reinterpret_cast<const char*>(data.data());
    if (std::memcmp(magic, "GVMH", 4) == 0) return ".gvm";
    if (std::memcmp(magic, "PVMH", 4) == 0
            || std::memcmp(magic, "PVM\0", 4) == 0
            || std::memcmp(magic, "PVLS", 4) == 0) {
        return ".pvm";
    }

    // Texturas GVR/PVR podem começar com GBIX ou outro chunk antes do
    // bloco de pixels; procure o cabeçalho real em todo o payload.
    for (size_t offset = 0; offset + 16 <= data.size(); ++offset) {
        const bool isGvrt =
                std::memcmp(data.data() + offset, "GVRT", 4) == 0;
        const bool isPvrt =
                std::memcmp(data.data() + offset, "PVRT", 4) == 0;
        if (!isGvrt && !isPvrt) continue;

        const uint16_t width = static_cast<uint16_t>(
                (static_cast<uint16_t>(data[offset + 12]) << 8u)
                | data[offset + 13]);
        const uint16_t height = static_cast<uint16_t>(
                (static_cast<uint16_t>(data[offset + 14]) << 8u)
                | data[offset + 15]);
        if (width != 0 && height != 0) {
            return isGvrt ? ".gvr" : ".pvr";
        }
    }

    return ".bin";
}

// Descomprime um fluxo PRS (bits de controle LSB-first, cópias curtas e longas).
bool DecompressPRS(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst) {
    constexpr size_t kMaxOutputSize = 256u * 1024u * 1024u;
    dst.clear();
    size_t inputPos = 0;

    auto fail = [&](const char* reason, int offset = 0, size_t length = 0) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "PRS inválido: %s (entrada=%zu/%zu, saída=%zu, offset=%d, len=%zu)",
                reason,
                inputPos,
                src.size(),
                dst.size(),
                offset,
                length);
        dst.clear();
        return false;
    };

    if (src.empty()) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "PRS inválido: arquivo vazio");
        return false;
    }

    uint8_t controlByte = 0;
    unsigned int controlBitsRemaining = 0;

    auto readBit = [&]() -> int {
        if (controlBitsRemaining == 0) {
            if (inputPos >= src.size()) return -1;
            controlByte = src[inputPos++];
            controlBitsRemaining = 8;
        }
        const int bit = static_cast<int>(controlByte & 1u);
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
        if (literalFlag < 0) return fail("faltou flag literal");

        if (literalFlag != 0) {
            const int value = readByte();
            if (value < 0) return fail("faltou byte literal");
            if (dst.size() >= kMaxOutputSize) return fail("limite de saída excedido");
            dst.push_back(static_cast<uint8_t>(value));
            continue;
        }

        const int longCopyFlag = readBit();
        if (longCopyFlag < 0) return fail("faltou flag de cópia");

        size_t copyLength;
        int copyOffset;
        if (longCopyFlag != 0) {
            const int lowByte = readByte();
            const int highByte = readByte();
            if (lowByte < 0 || highByte < 0) {
                return fail("faltaram bytes da cópia longa");
            }

            const uint16_t packedOffset = static_cast<uint16_t>(
                    lowByte | (highByte << 8));
            if (packedOffset == 0) return !dst.empty();

            const uint16_t encodedOffset = packedOffset >> 3u;
            // O campo de deslocamento PRS é sempre um offset negativo de
            // 13 bits relativo ao fim do buffer descompactado.
            copyOffset = static_cast<int>(encodedOffset) - 0x2000;

            const unsigned int lengthCode = packedOffset & 0x7u;
            if (lengthCode == 0) {
                const int extendedLength = readByte();
                if (extendedLength < 0) return fail("faltou comprimento estendido");
                copyLength = static_cast<size_t>(extendedLength) + 1;
            } else {
                copyLength = static_cast<size_t>(lengthCode) + 2;
            }
        } else {
            const int bit0 = readBit();
            const int bit1 = readBit();
            const int offsetByte = readByte();
            if (bit0 < 0 || bit1 < 0 || offsetByte < 0) {
                return fail("faltaram dados da cópia curta");
            }

            // O PRS lê o primeiro bit como o bit mais significativo.
            copyLength = static_cast<size_t>(((bit0 << 1) | bit1) + 2);
            // Cópias curtas usam um deslocamento negativo de 8 bits;
            // o byte armazenado representa 256 + offset (-256..-1).
            copyOffset = offsetByte - 0x100;
        }

        if (copyOffset >= 0) {
            return fail("offset de cópia não é negativo", copyOffset, copyLength);
        }
        if (static_cast<size_t>(-copyOffset) > dst.size()) {
            return fail("offset aponta antes do início da saída", copyOffset, copyLength);
        }
        if (copyLength > kMaxOutputSize - dst.size()) {
            return fail("cópia excede limite de saída", copyOffset, copyLength);
        }

        size_t copyPos = dst.size() - static_cast<size_t>(-copyOffset);
        for (size_t i = 0; i < copyLength; ++i) {
            if (copyPos >= dst.size()) {
                return fail("cópia ultrapassou a saída disponível", copyOffset, copyLength);
            }
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

static bool processSinglePRS(const std::string& srcFilePath) {
    const size_t slash = srcFilePath.find_last_of('/');
    const std::string sourceDir = slash == std::string::npos
            ? "."
            : srcFilePath.substr(0, slash);
    const std::string fileName = slash == std::string::npos
            ? srcFilePath
            : srcFilePath.substr(slash + 1);

    const std::vector<uint8_t> compressedData =
            lerArquivoBinario(srcFilePath);
    if (compressedData.empty()) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "PRS vazio ou ilegível: %s",
                srcFilePath.c_str());
        return false;
    }

    std::vector<uint8_t> decompressedData;
    if (!DecompressPRS(compressedData, decompressedData)
            || decompressedData.empty()) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "Falha ao descompactar PRS: %s (%zu bytes)",
                fileName.c_str(),
                compressedData.size());
        return false;
    }

    const size_t lastDot = fileName.find_last_of('.');
    std::string cleanName = lastDot == std::string::npos
            ? fileName
            : fileName.substr(0, lastDot);
    if (getFileExtension(cleanName).empty()) {
        cleanName += inferResourceExtension(decompressedData);
    }
    if (getFileExtension(cleanName) == ".rgba") {
        cleanName.replace(cleanName.find_last_of('.'), std::string::npos, ".tga");
    }

    const std::string outputPath = sourceDir + "/" + cleanName;
    bool writeSucceeded = false;
    if (getFileExtension(cleanName) == ".tga") {
        writeSucceeded = salvarTGA(outputPath, decompressedData, 256, 256);
    } else {
        std::ofstream outFile(outputPath, std::ios::binary | std::ios::trunc);
        if (outFile.is_open()) {
            outFile.write(
                    reinterpret_cast<const char*>(decompressedData.data()),
                    static_cast<std::streamsize>(decompressedData.size()));
            writeSucceeded = outFile.good();
            outFile.close();
            writeSucceeded = writeSucceeded && outFile.good();
        }
    }

    if (!writeSucceeded) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                PRS_PIPELINE_LOG_TAG,
                "Falha ao salvar saída PRS: %s",
                outputPath.c_str());
        return false;
    }

    struct stat outputStatus {};
    const bool outputCommitted =
            stat(outputPath.c_str(), &outputStatus) == 0
            && outputStatus.st_size > 0;
    if (!outputCommitted) return false;

    if (std::remove(srcFilePath.c_str()) != 0) {
        __android_log_print(
                ANDROID_LOG_WARN,
                PRS_PIPELINE_LOG_TAG,
                "Saída criada, mas não foi possível remover o PRS: %s",
                fileName.c_str());
    }
    return true;
}

void processPipelineWithDecompressed(
        const std::string& baseDirPath,
        const std::function<void()>& onFileTranslated) {
    const std::string tempPath = baseDirPath + "/temp";
    createDecompressedDirectory(baseDirPath);

    const std::vector<std::string> sourceDirs = {
            tempPath,
            tempPath + "/textures",
            tempPath + "/models",
            tempPath + "/audio",
            tempPath + "/ui",
            tempPath + "/misc"
    };
    std::vector<std::string> pendingFiles;

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
            if (stat(srcFilePath.c_str(), &sourceStatus) == 0
                    && S_ISREG(sourceStatus.st_mode)) {
                pendingFiles.push_back(srcFilePath);
            }
        }
        closedir(dir);
    }

    const unsigned int workerCount = std::max(
            2u,
            std::min(
                    4u,
                    std::thread::hardware_concurrency() == 0
                            ? 2u
                            : std::thread::hardware_concurrency()));
    std::vector<std::future<bool>> futures;
    futures.reserve(workerCount);

    for (const std::string& filePath : pendingFiles) {
        futures.push_back(std::async(
                std::launch::async,
                processSinglePRS,
                filePath));

        if (futures.size() >= workerCount) {
            const bool translated = futures.front().get();
            futures.erase(futures.begin());
            if (translated && onFileTranslated) onFileTranslated();
        }
    }

    for (std::future<bool>& future : futures) {
        if (future.get() && onFileTranslated) onFileTranslated();
    }
}

