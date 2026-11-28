#include "pvm_parser.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <android/log.h>

namespace fs = std::filesystem;

#define LOG_TAG "SA2B_Parser"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// Estrutura do cabeçalho PVM (Padrão SEGA PVRT/PVM)
#pragma pack(push, 1)
struct PVMHeader {
    char magic[4];       // Geralmente "PVM\0" ou "PVLS"
    uint32_t totalSize;  // Tamanho total do arquivo
    uint32_t entryCount; // Número de texturas PVR empacotadas
};

// Entrada individual na tabela do PVM
struct PVMEntryHeader {
    uint32_t flags;      // Flags da textura (Global Index, etc.)
    uint32_t dataOffset; // Offset onde o arquivo PVR começa dentro do PVM
    char name[28];       // Nome da textura (terminado em null)
};
#pragma pack(pop)

static uint32_t readLittleEndian32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0])
            | (static_cast<uint32_t>(data[1]) << 8u)
            | (static_cast<uint32_t>(data[2]) << 16u)
            | (static_cast<uint32_t>(data[3]) << 24u);
}

static bool findGvrGlobalChunkStart(
        const std::vector<uint8_t>& data,
        size_t gvrtOffset,
        size_t& chunkStart) {
    const size_t searchStart = gvrtOffset > 256u ? gvrtOffset - 256u : 0u;
    for (size_t candidate = searchStart;
            candidate + 8u <= gvrtOffset; ++candidate) {
        const bool isGbix = std::memcmp(data.data() + candidate, "GBIX", 4) == 0;
        const bool isGcix = std::memcmp(data.data() + candidate, "GCIX", 4) == 0;
        if (!isGbix && !isGcix) continue;

        const uint32_t chunkSize = readLittleEndian32(data.data() + candidate + 4u);
        if (chunkSize >= 8u
                && static_cast<size_t>(chunkSize) == gvrtOffset - candidate - 8u) {
            chunkStart = candidate;
            return true;
        }
    }
    return false;
}

// Extrai PVRs e, se solicitado, devolve a tabela para exportação JSON.
static bool ExtractPVMContainerInternal(
        const std::vector<uint8_t>& pvmFileData,
        std::vector<ExtractedTexture>& outTextures,
        std::vector<PVMTextureMetadata>* metadataOut) {
    if (pvmFileData.size() < sizeof(PVMHeader)) {
        LOGE("Erro: Arquivo PVM muito pequeno para conter um cabeçalho válido.");
        return false;
    }

    const PVMHeader* header = reinterpret_cast<const PVMHeader*>(pvmFileData.data());

    const bool isPvm = std::memcmp(header->magic, "PVMH", 4) == 0
            || std::memcmp(header->magic, "PVM\0", 4) == 0
            || std::memcmp(header->magic, "PVLS", 4) == 0;
    const bool isGvm = std::memcmp(header->magic, "GVMH", 4) == 0;
    if (!isPvm && !isGvm) {
        LOGW("Assinatura de container não convencional ('%c%c%c%c'). Tentando ler assim mesmo...",
             header->magic[0], header->magic[1],
             header->magic[2], header->magic[3]);
    }

    LOGI("=== Lendo Container PVM/GVM ===");

    // GVM não possui necessariamente a mesma tabela de entradas do PVM.
    // Localiza os blocos GVRT/PVRT diretamente para não salvar o container
    // como um recurso final quando a tabela variar entre versões do jogo.
    if (isGvm) {
        std::vector<size_t> textureOffsets;
        std::vector<const char*> textureMagics;
        for (size_t offset = sizeof(PVMHeader); offset + 4 <= pvmFileData.size(); ++offset) {
            const char* magic = reinterpret_cast<const char*>(
                    pvmFileData.data() + offset);
            if (std::memcmp(magic, "GVRT", 4) == 0) {
                textureOffsets.push_back(offset);
                textureMagics.push_back("GVRT");
            } else if (std::memcmp(magic, "PVRT", 4) == 0) {
                textureOffsets.push_back(offset);
                textureMagics.push_back("PVRT");
            }
        }

        std::vector<size_t> textureChunkStarts(textureOffsets);
        std::vector<bool> hasGlobalChunk(textureOffsets.size(), false);
        for (size_t index = 0; index < textureOffsets.size(); ++index) {
            if (std::strcmp(textureMagics[index], "GVRT") == 0) {
                size_t globalChunkStart = 0;
                if (findGvrGlobalChunkStart(
                            pvmFileData,
                            textureOffsets[index],
                            globalChunkStart)) {
                    textureChunkStarts[index] = globalChunkStart;
                    hasGlobalChunk[index] = true;
                }
            }
        }

        for (size_t index = 0; index < textureOffsets.size(); ++index) {
            const size_t textureStart = textureOffsets[index];
            const size_t startOffset = textureChunkStarts[index];
            const size_t endOffset = index + 1 < textureOffsets.size()
                    ? textureChunkStarts[index + 1]
                    : pvmFileData.size();
            if (endOffset <= textureStart) continue;

            ExtractedTexture texture;
            const bool isGvrTexture =
                    std::strcmp(textureMagics[index], "GVRT") == 0;
            texture.name = "texture_" + std::to_string(index)
                    + (isGvrTexture ? ".gvr" : ".pvr");

            if (isGvrTexture && !hasGlobalChunk[index]) {
                static const uint8_t emptyGcix[16] = {
                        'G', 'C', 'I', 'X', 8, 0, 0, 0,
                        0, 0, 0, 0, 0, 0, 0, 0
                };
                texture.pvrData.insert(
                        texture.pvrData.end(),
                        std::begin(emptyGcix),
                        std::end(emptyGcix));
                texture.pvrData.insert(
                        texture.pvrData.end(),
                        pvmFileData.begin()
                                + static_cast<std::ptrdiff_t>(textureStart),
                        pvmFileData.begin()
                                + static_cast<std::ptrdiff_t>(endOffset));
            } else {
                texture.pvrData.assign(
                        pvmFileData.begin()
                                + static_cast<std::ptrdiff_t>(startOffset),
                        pvmFileData.begin()
                                + static_cast<std::ptrdiff_t>(endOffset));
            }
            outTextures.push_back(std::move(texture));
        }

        if (metadataOut != nullptr) {
            metadataOut->clear();
            for (const size_t offset : textureOffsets) {
                metadataOut->push_back({"", static_cast<uint32_t>(offset)});
            }
        }

        if (!outTextures.empty()) {
            LOGI("GVM extraído por blocos GVRT/PVRT: %zu textura(s).",
                 outTextures.size());
            return true;
        }

        LOGW("Nenhum bloco GVRT/PVRT encontrado no GVM.");
        return false;
    }

    LOGI("Total de texturas encontradas no pacote: %u", header->entryCount);

    std::vector<PVMTextureMetadata> entriesList;

    const uint8_t* ptr = pvmFileData.data() + sizeof(PVMHeader);
    const uint8_t* endPtr = pvmFileData.data() + pvmFileData.size();

    // Primeira passagem: coleta nomes e offsets válidos da tabela.
    for (uint32_t i = 0; i < header->entryCount; ++i) {
        if (static_cast<size_t>(endPtr - ptr) < sizeof(PVMEntryHeader)) {
            LOGE("Erro: Fim do arquivo inesperado ao ler a entrada %u", i);
            return false;
        }

        const PVMEntryHeader* entry = reinterpret_cast<const PVMEntryHeader*>(ptr);
        const char* nameEnd = std::find(
                entry->name, entry->name + sizeof(entry->name), '\0');
        std::string texName(entry->name, nameEnd);

        LOGI(" - Textura [%u]: %s | Offset: %u",
             i, texName.c_str(), entry->dataOffset);

        if (entry->dataOffset > 0
                && static_cast<size_t>(entry->dataOffset) < pvmFileData.size()) {
            entriesList.push_back({texName, entry->dataOffset});
        }
        ptr += sizeof(PVMEntryHeader);
    }

    // Segunda passagem: extrai cada bloco até o próximo offset ou o fim do PVM.
    for (size_t i = 0; i < entriesList.size(); ++i) {
        const size_t startOffset = entriesList[i].offset;
        const size_t endOffset = i + 1 < entriesList.size()
                ? entriesList[i + 1].offset
                : pvmFileData.size();

        if (endOffset <= startOffset) {
            LOGW("Offset inválido para textura: %s", entriesList[i].name.c_str());
            continue;
        }

        const size_t pvrSize = endOffset - startOffset;
        ExtractedTexture extTex;
        extTex.name = entriesList[i].name;
        extTex.pvrData.resize(pvrSize);
        std::memcpy(
                extTex.pvrData.data(),
                pvmFileData.data() + startOffset,
                pvrSize);

        outTextures.push_back(extTex);
        LOGI(" - Extraído PVR: %s (Tamanho: %zu bytes)",
             extTex.name.c_str(), pvrSize);
    }

    if (metadataOut != nullptr) {
        *metadataOut = entriesList;
    }
    return true;
}

bool ExtractPVMContainer(
        const std::vector<uint8_t>& pvmFileData,
        std::vector<ExtractedTexture>& outTextures) {
    return ExtractPVMContainerInternal(pvmFileData, outTextures, nullptr);
}

static void writeJsonString(std::ofstream& output, const std::string& value) {
    static constexpr char hexDigits[] = "0123456789abcdef";
    output.put('"');
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (ch < 0x20) {
                    output << "\\u00"
                           << hexDigits[(ch >> 4) & 0x0f]
                           << hexDigits[ch & 0x0f];
                } else {
                    output.put(static_cast<char>(ch));
                }
        }
    }
    output.put('"');
}

bool SalvarPVMEmJSON(
        const std::string& nomePVM,
        const std::vector<PVMTextureMetadata>& entriesList,
        const fs::path& pastaDestino) {
    std::error_code error;
    fs::create_directories(pastaDestino, error);
    if (error) {
        LOGE("Não foi possível criar a pasta de metadados: %s",
             pastaDestino.string().c_str());
        return false;
    }

    fs::path nomeJson = fs::path(nomePVM).filename();
    if (nomeJson.extension() != ".json") {
        nomeJson.replace_extension(".json");
    }
    const fs::path caminhoJson = pastaDestino / nomeJson;

    std::ofstream arquivo(caminhoJson, std::ios::out | std::ios::trunc);
    if (!arquivo.is_open()) {
        LOGE("Erro ao criar o ficheiro JSON para o PVM: %s",
             caminhoJson.string().c_str());
        return false;
    }

    arquivo << "{\n  \"pvm_container\": ";
    writeJsonString(arquivo, nomePVM);
    arquivo << ",\n  \"total_textures\": " << entriesList.size()
            << ",\n  \"textures\": [\n";

    for (size_t i = 0; i < entriesList.size(); ++i) {
        arquivo << "    {\n      \"index\": " << i
                << ",\n      \"name\": ";
        writeJsonString(arquivo, entriesList[i].name);
        arquivo << ",\n      \"offset\": " << entriesList[i].offset
                << "\n    }";
        if (i + 1 < entriesList.size()) {
            arquivo << ",";
        }
        arquivo << "\n";
    }

    arquivo << "  ]\n}\n";
    arquivo.close();
    if (!arquivo) {
        LOGE("Falha ao terminar a gravação do JSON: %s",
             caminhoJson.string().c_str());
        return false;
    }

    LOGI("Metadados PVM salvos em JSON: %s",
         caminhoJson.filename().string().c_str());
    return true;
}

bool ExtractPVMContainer(
        const std::vector<uint8_t>& pvmFileData,
        std::vector<ExtractedTexture>& outTextures,
        const std::string& nomePVM,
        const fs::path& pastaDestino) {
    std::vector<PVMTextureMetadata> entriesList;
    if (!ExtractPVMContainerInternal(pvmFileData, outTextures, &entriesList)) {
        return false;
    }
    return SalvarPVMEmJSON(nomePVM, entriesList, pastaDestino);
}