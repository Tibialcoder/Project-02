#include "pvm_parser.h"

#include <algorithm>
#include <cstring>
#include <fstream>
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

    // Verifica se o magic bate (ex: "PVM")
    if (header->magic[0] != 'P' && header->magic[0] != 'p') {
        LOGW("Aviso: Assinatura do PVM não convencional ('%c%c%c'). Tentando ler assim mesmo...",
             header->magic[0], header->magic[1], header->magic[2]);
    }

    LOGI("=== Lendo Container PVM ===");
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