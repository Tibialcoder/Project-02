#include "resource_file_manager.h"

#include <android/log.h>
#include <cerrno>
#include <sys/stat.h>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

#define RESOURCE_FILE_LOG_TAG "SA2B_RESOURCE_FILES"

void createDecompressedDirectory(const std::string& baseDirPath) {
    const std::string tempPath = baseDirPath + "/temp";
    const std::string decompressedPath = tempPath + "/decompressed";

    if (mkdir(tempPath.c_str(), 0777) != 0 && errno != EEXIST) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                RESOURCE_FILE_LOG_TAG,
                "Não foi possível criar a pasta temp: %s",
                tempPath.c_str());
        return;
    }

    if (mkdir(decompressedPath.c_str(), 0777) != 0 && errno != EEXIST) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                RESOURCE_FILE_LOG_TAG,
                "Não foi possível criar a pasta: %s",
                decompressedPath.c_str());
        return;
    }

    __android_log_print(
            ANDROID_LOG_INFO,
            RESOURCE_FILE_LOG_TAG,
            "Pasta de segurança /temp/decompressed verificada/criada.");
}

bool createDirectory(const std::string& path) {
    int status = mkdir(path.c_str(), 0777);
    return (status == 0 || errno == EEXIST);
}

// Estrutura para classificar com base na extensão ou nome
std::string classifyFile(const std::string& filename) {
    std::string lowerName = filename;
    for (char& c : lowerName) c = tolower(c);

    // 1. Áudio
    if (lowerName.find(".adx") != std::string::npos) {
        return "audio";
    }

    // Bancos text_*.bin são tabelas de texto, não recursos de textura.
    if (lowerName.rfind("text_", 0) == 0) {
        return "misc";
    }

    // 2. Texturas (.pvm, .prs que sejam de textura, ou imagens)
    if (lowerName.find(".pvm") != std::string::npos ||
        lowerName.find("tex") != std::string::npos ||
        lowerName.find("bg") != std::string::npos) {
        return "textures";
    }

    // Recursos de UI (menus, fontes e mensagens) seguem como texturas comuns.
    if (lowerName.find("ui") != std::string::npos ||
        lowerName.find("menu") != std::string::npos ||
        lowerName.find("font") != std::string::npos ||
        lowerName.find("message") != std::string::npos) {
        return "textures";
    }

    // 4. Modelos 3D (.sa2, .nj, .njm)
    if (lowerName.find(".sa2") != std::string::npos ||
        lowerName.find(".nj") != std::string::npos ||
        lowerName.find(".njm") != std::string::npos) {
        return "models";
    }

    // 5. Arquivos .prs genéricos (podem ser modelos ou texturas compactadas)
    // Se não tiver outra dica no nome, podemos mandar para models ou misc por padrão
    if (lowerName.find(".prs") != std::string::npos) {
        return "models";
    }

    return "misc";
}

bool extrairParaTemp(
        const std::vector<uint8_t>& dados,
        const fs::path& caminhoTemp) {
    std::error_code error;
    fs::create_directories(caminhoTemp.parent_path(), error);
    if (error) return false;

    std::ofstream arquivo(caminhoTemp, std::ios::binary | std::ios::trunc);
    if (!arquivo.is_open()) return false;
    arquivo.write(
            reinterpret_cast<const char*>(dados.data()),
            static_cast<std::streamsize>(dados.size()));
    arquivo.close();
    return arquivo.good();
}

bool moverParaPastaDefinitiva(
        const fs::path& caminhoTemp,
        const fs::path& caminhoDestinoFinal) {
    std::error_code error;
    fs::create_directories(caminhoDestinoFinal.parent_path(), error);
    if (error) return false;

    fs::rename(caminhoTemp, caminhoDestinoFinal, error);
    if (!error) return true;

    error.clear();
    fs::copy_file(
            caminhoTemp,
            caminhoDestinoFinal,
            fs::copy_options::overwrite_existing,
            error);
    if (error) return false;

    error.clear();
    fs::remove(caminhoTemp, error);
    return !error;
}

