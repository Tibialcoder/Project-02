#include "resource_pipeline.h"
#include "resource_file_manager.h"
#include "prs_decompressor.h"
#include "texture_translation.h"

#include <android/log.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace fs = std::filesystem;
#define MODEL_PIPELINE_LOG_TAG "SA2B_MODEL_PIPELINE"
#define SUPPORTED_PIPELINE_LOG_TAG "SA2B"

bool temTradutor(const fs::path& caminho) {
    // A descompressão PRS é o tradutor disponível neste pipeline.
    return getFileExtension(caminho.filename().string()) == ".prs";
}

bool aplicarTraducao(const fs::path& caminhoTemp, fs::path& caminhoProcessado) {
    const std::vector<uint8_t> dadosComprimidos =
            lerArquivoBinario(caminhoTemp.string());
    if (dadosComprimidos.empty()) return false;

    std::vector<uint8_t> dadosDescomprimidos;
    if (!DecompressPRS(dadosComprimidos, dadosDescomprimidos)
            || dadosDescomprimidos.empty()) {
        return false;
    }

    const std::string fileName = caminhoTemp.filename().string();
    const size_t lastDot = fileName.find_last_of('.');
    const std::string cleanName = lastDot == std::string::npos
            ? fileName
            : fileName.substr(0, lastDot);
    caminhoProcessado = caminhoTemp.parent_path().parent_path()
            / "decompressed" / cleanName;

    std::error_code error;
    fs::create_directories(caminhoProcessado.parent_path(), error);
    if (error) return false;

    std::ofstream output(caminhoProcessado, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) return false;
    output.write(
            reinterpret_cast<const char*>(dadosDescomprimidos.data()),
            static_cast<std::streamsize>(dadosDescomprimidos.size()));
    output.close();
    return output.good();
}

bool processarRecurso(
        const fs::path& caminhoOriginal,
        const fs::path& pastaDefinitiva) {
    const std::string nomeFicheiro = caminhoOriginal.filename().string();
    const std::string extensao = getFileExtension(nomeFicheiro);
    // O resultado descompactado permanece na mesma subpasta temporária
    // em que o arquivo original foi organizado (models, textures, etc.).
    const fs::path pastaTemp = caminhoOriginal.parent_path();

    // Lê o recurso e aplica a tradução/descompressão disponível quando existir.
    std::vector<uint8_t> dadosProcessados;
    if (extensao == ".prs") {
        __android_log_print(
                ANDROID_LOG_INFO,
                "SA2B_PIPELINE",
                "Descomprimindo PRS: %s",
                nomeFicheiro.c_str());

        const std::vector<uint8_t> dadosComprimidos =
                lerArquivoBinario(caminhoOriginal.string());
        if (dadosComprimidos.empty()) {
            __android_log_print(
                    ANDROID_LOG_ERROR,
                    "SA2B_PIPELINE",
                    "Não foi possível ler o arquivo de origem: %s",
                    nomeFicheiro.c_str());
            return false;
        }

        const bool sucessoTraducao =
                DecompressPRS(dadosComprimidos, dadosProcessados);
        if (!sucessoTraducao) {
            __android_log_print(
                    ANDROID_LOG_ERROR,
                    "SA2B_PIPELINE",
                    "A tradução/descompressão falhou; origem preservada: %s",
                    nomeFicheiro.c_str());
            return false;
        }
    } else {
        dadosProcessados = lerArquivoBinario(caminhoOriginal.string());
    }

    // Nunca encaminha um buffer vazio para o temporário nem para a pasta final.
    if (dadosProcessados.empty()) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                "SA2B_PIPELINE",
                "Tentativa de salvar recurso vazio: %s",
                nomeFicheiro.c_str());
        return false;
    }

    if (!temTradutor(caminhoOriginal) && extensao != ".rgba") {
        __android_log_print(
                ANDROID_LOG_DEBUG,
                "SA2B_PIPELINE",
                "Sem tradução para %s; mantendo dados originais.",
                nomeFicheiro.c_str());
    }

    std::string nomeProcessado = nomeFicheiro;
    if (extensao == ".prs") {
        const size_t lastDot = nomeProcessado.find_last_of('.');
        nomeProcessado = nomeProcessado.substr(0, lastDot);

        // PRS é a compressão, não necessariamente a extensão do conteúdo.
        // Preserva extensões internas (por exemplo, arquivo.pak.prs) e usa
        // .bin como extensão neutra quando o nome original não informa o formato.
        if (fs::path(nomeProcessado).extension().empty()) {
            nomeProcessado += ".bin";
        }
    }

    std::error_code error;
    fs::create_directories(pastaTemp, error);
    if (error) return false;

    const fs::path caminhoTemp = pastaTemp / nomeProcessado;
    if (!extrairParaTemp(dadosProcessados, caminhoTemp)) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                "SA2B_PIPELINE",
                "Falha ao salvar no temporário: %s",
                caminhoTemp.string().c_str());
        return false;
    }

    const fs::path caminhoFinal = pastaDefinitiva / nomeProcessado;
    bool saved = false;
    if (getFileExtension(nomeProcessado) == ".rgba"
            && pastaDefinitiva.filename() == "textures") {
        saved = salvarRGBA(caminhoFinal.string(), dadosProcessados, 256, 256);
    } else {
        std::error_code copyError;
        fs::create_directories(caminhoFinal.parent_path(), copyError);
        if (!copyError) {
            fs::copy_file(
                    caminhoTemp,
                    caminhoFinal,
                    fs::copy_options::overwrite_existing,
                    copyError);
        }
        saved = !copyError;
    }

    if (!saved) return false;

    // Remove a cópia extraída da categoria somente depois do destino final estar pronto.
    if (caminhoOriginal != caminhoTemp) {
        fs::remove(caminhoOriginal, error);
    }

    __android_log_print(
            ANDROID_LOG_INFO,
            "SA2B_PIPELINE",
            "Recurso processado: %s",
            caminhoFinal.string().c_str());
    return true;
}

bool extensaoPermitida(const fs::path& caminho) {
    std::string ext = caminho.extension().string();
    std::transform(
            ext.begin(),
            ext.end(),
            ext.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext == ".prs" || ext == ".rgba" || ext == ".bin";
}

void processarPastaRecursos(
        const fs::path& pastaOrigem,
        const fs::path& pastaDefinitiva) {
    std::error_code error;
    if (!fs::exists(pastaOrigem, error) || error) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                "SA2B_PIPELINE",
                "Pasta de origem não encontrada: %s",
                pastaOrigem.string().c_str());
        return;
    }

    fs::directory_iterator entry(pastaOrigem, error);
    const fs::directory_iterator end;
    if (error) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                "SA2B_PIPELINE",
                "Não foi possível listar a pasta: %s",
                pastaOrigem.string().c_str());
        return;
    }

    // Captura a lista antes de processar para não incluir arquivos recém-gerados.
    std::vector<fs::path> arquivosDaPasta;
    while (entry != end) {
        arquivosDaPasta.push_back(entry->path());
        entry.increment(error);
        if (error) {
            __android_log_print(
                    ANDROID_LOG_ERROR,
                    "SA2B_PIPELINE",
                    "Erro ao percorrer a pasta: %s",
                    pastaOrigem.string().c_str());
            return;
        }
    }

    for (const fs::path& caminhoArquivo : arquivosDaPasta) {
        std::error_code statusError;
        if (!fs::is_regular_file(caminhoArquivo, statusError)) continue;

        if (!extensaoPermitida(caminhoArquivo)) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    "SA2B_PIPELINE",
                    "Formato ignorado e preservado em temp: %s",
                    caminhoArquivo.filename().string().c_str());
            continue;
        }

        __android_log_print(
                ANDROID_LOG_INFO,
                "SA2B_PIPELINE",
                "Processando recurso: %s",
                caminhoArquivo.filename().string().c_str());
        if (!processarRecurso(caminhoArquivo, pastaDefinitiva)) {
            __android_log_print(
                    ANDROID_LOG_ERROR,
                    "SA2B_PIPELINE",
                    "Falha ao processar; arquivo mantido em temp: %s",
                    caminhoArquivo.string().c_str());
        }
    }
}

void processOnlySupportedFiles(const std::string& baseDirPath) {
    const std::string tempPath = baseDirPath + "/temp/textures";
    DIR* dir = opendir(tempPath.c_str());
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string fileName = entry->d_name;
        if (fileName == "." || fileName == "..") continue;

        const std::string extension = getFileExtension(fileName);
        const size_t lastDot = fileName.find_last_of('.');
        const std::string baseName = lastDot == std::string::npos
                ? fileName
                : fileName.substr(0, lastDot);

        if (extension == ".pvm") {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    SUPPORTED_PIPELINE_LOG_TAG,
                    "PVM reconhecido para tradução: %s (base: %s)",
                    fileName.c_str(),
                    baseName.c_str());
            // A extração do PVM ainda precisa ser ligada ao parser PVM neste target.
        } else if (extension == ".sa2" || extension == ".nj") {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    SUPPORTED_PIPELINE_LOG_TAG,
                    "Modelo reconhecido: %s",
                    fileName.c_str());
        } else {
            __android_log_print(
                    ANDROID_LOG_DEBUG,
                    SUPPORTED_PIPELINE_LOG_TAG,
                    "Ignorando arquivo sem tradutor: %s",
                    fileName.c_str());
        }
    }

    closedir(dir);
}

bool modelFileExists(const std::string& path) {
    struct stat fileStatus;
    return stat(path.c_str(), &fileStatus) == 0;
}

void processModelsWithChecks(const std::string& baseDirPath) {
    const std::string tempModelsPath = baseDirPath + "/temp/models";
    const std::string finalModelsPath = baseDirPath + "/models";

    DIR* dir = opendir(tempModelsPath.c_str());
    if (!dir) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                MODEL_PIPELINE_LOG_TAG,
                "Pasta temp/models não encontrada.");
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
        const std::string srcFilePath = tempModelsPath + "/" + fileName;
        const std::string dstFilePath = finalModelsPath + "/" + baseName + ".bin";

        if (modelFileExists(dstFilePath)) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    MODEL_PIPELINE_LOG_TAG,
                    "Modelo já processado. Limpando temporário: %s",
                    fileName.c_str());
            std::remove(srcFilePath.c_str());
            continue;
        }

        FILE* inFile = std::fopen(srcFilePath.c_str(), "rb");
        if (!inFile) continue;

        if (std::fseek(inFile, 0, SEEK_END) != 0) {
            std::fclose(inFile);
            continue;
        }
        const long fileSize = std::ftell(inFile);
        if (fileSize <= 0 || std::fseek(inFile, 0, SEEK_SET) != 0) {
            std::fclose(inFile);
            continue;
        }

        std::vector<uint8_t> modelData(static_cast<size_t>(fileSize));
        const size_t bytesRead = std::fread(modelData.data(), 1, modelData.size(), inFile);
        std::fclose(inFile);
        if (bytesRead != modelData.size()) {
            __android_log_print(
                    ANDROID_LOG_WARN,
                    MODEL_PIPELINE_LOG_TAG,
                    "Leitura incompleta do modelo: %s",
                    fileName.c_str());
            continue;
        }

        FILE* outFile = std::fopen(dstFilePath.c_str(), "wb");
        if (!outFile) continue;

        const size_t bytesWritten = std::fwrite(
                modelData.data(), 1, modelData.size(), outFile);
        const bool outputClosed = std::fclose(outFile) == 0;

        if (bytesWritten == modelData.size() && outputClosed) {
            __android_log_print(
                    ANDROID_LOG_INFO,
                    MODEL_PIPELINE_LOG_TAG,
                    "Modelo processado e salvo: %s.bin",
                    baseName.c_str());

            if (std::remove(srcFilePath.c_str()) == 0) {
                __android_log_print(
                        ANDROID_LOG_DEBUG,
                        MODEL_PIPELINE_LOG_TAG,
                        "Modelo bruto removido da temp: %s",
                        fileName.c_str());
            }
        }
    }

    closedir(dir);
}

