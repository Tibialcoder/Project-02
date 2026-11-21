#include <vector>
#include <string>
#include <cstdint>
#include <android/log.h>

#define LOG_TAG "SA2B_Parser"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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

// Estrutura para armazenar a textura PVR extraída
struct ExtractedTexture {
    std::string name;
    std::vector<uint8_t> pvrData;
};

// Função principal que desacopla o PVM nos PVRs internos
bool ExtractPVMContainer(const std::vector<uint8_t>& pvmFileData, std::vector<ExtractedTexture>& outTextures) {
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

    // O ponteiro para a primeira entrada logo após o cabeçalho principal
    const uint8_t* ptr = pvmFileData.data() + sizeof(PVMHeader);
    const uint8_t* endPtr = pvmFileData.data() + pvmFileData.size();

    for (uint32_t i = 0; i < header->entryCount; ++i) {
        if (ptr + sizeof(PVMEntryHeader) > endPtr) {
            LOGE("Erro: Fim do arquivo inesperado ao ler a entrada %u", i);
            return false;
        }

        const PVMEntryHeader* entry = reinterpret_cast<const PVMEntryHeader*>(ptr);

        // O nome da textura armazenada (garantindo o encerramento com '\0')
        std::string texName(entry->name, strnlen(entry->name, sizeof(entry->name)));

        LOGI(" - Textura [%u]: %s | Offset: %u", i, texName.c_str(), entry->dataOffset);

        // Se o dataOffset estiver definido, extraímos os bytes do PVR correspondente
        if (entry->dataOffset > 0 && entry->dataOffset < pvmFileData.size()) {
            // Nota: Em alguns formatos PVM, os offsets apontam diretamente para os dados PVR.
            // Precisamos calcular o tamanho olhando para a próxima entrada ou o fim do arquivo.
            // Para simplificar a extração inicial, vamos capturar a região de dados:

            // (Opcional) Localiza o próximo offset para definir o tamanho exato do PVR atual
            // ...
        }

        // Avança para a próxima entrada no cabeçalho/tabela do PVM
        ptr += sizeof(PVMEntryHeader);
    }

    return true;
}