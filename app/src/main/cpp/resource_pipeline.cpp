#include "resource_pipeline.h"
#include "resource_file_manager.h"
#include "prs_decompressor.h"
#include "texture_translation.h"
#include "pvm_parser.h"

#include <android/log.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace fs = std::filesystem;
#define MODEL_PIPELINE_LOG_TAG "SA2B_MODEL_PIPELINE"
#define SUPPORTED_PIPELINE_LOG_TAG "SA2B"

static uint32_t readBigEndian32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24u)
            | (static_cast<uint32_t>(data[1]) << 16u)
            | (static_cast<uint32_t>(data[2]) << 8u)
            | static_cast<uint32_t>(data[3]);
}

static void writeJsonString(std::ofstream& output, const std::string& value) {
    output.put('"');
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (ch < 0x20) {
                    output << "\\u00";
                    static constexpr char digits[] = "0123456789abcdef";
                    output.put(digits[(ch >> 4u) & 0x0f]);
                    output.put(digits[ch & 0x0f]);
                } else {
                    output.put(static_cast<char>(ch));
                }
        }
    }
    output.put('"');
}

static bool exportTextBankJson(
        const std::vector<uint8_t>& data,
        const std::string& fileName,
        const fs::path& outputDirectory) {
    if (data.size() < 4) return false;

    const uint32_t tableSize = readBigEndian32(data.data());
    if (tableSize < 4 || tableSize > data.size() || tableSize % 4 != 0) {
        __android_log_print(
                ANDROID_LOG_WARN,
                SUPPORTED_PIPELINE_LOG_TAG,
                "Tabela de offsets inválida no banco de texto: %s",
                fileName.c_str());
        return false;
    }

    const size_t entryCount = tableSize / 4;
    std::vector<uint32_t> offsets(entryCount);
    for (size_t index = 0; index < entryCount; ++index) {
        offsets[index] = readBigEndian32(data.data() + index * 4);
        if (offsets[index] < tableSize || offsets[index] > data.size()) {
            __android_log_print(
                    ANDROID_LOG_WARN,
                    SUPPORTED_PIPELINE_LOG_TAG,
                    "Offset inválido no banco de texto %s, entrada %zu",
                    fileName.c_str(),
                    index);
            return false;
        }
    }

    std::error_code error;
    fs::create_directories(outputDirectory, error);
    if (error) return false;

    fs::path jsonName(fileName);
    jsonName.replace_extension(".json");
    const fs::path jsonPath = outputDirectory / jsonName;
    std::ofstream output(jsonPath, std::ios::out | std::ios::trunc);
    if (!output.is_open()) return false;

    output << "{\n  \"source\": ";
    writeJsonString(output, fileName);
    output << ",\n  \"entries\": [\n";

    for (size_t index = 0; index < entryCount; ++index) {
        const size_t start = offsets[index];
        size_t end = data.size();
        for (size_t next = index + 1; next < entryCount; ++next) {
            if (offsets[next] > start) {
                end = offsets[next];
                break;
            }
        }

        std::string text;
        if (start < end) {
            size_t textStart = start;
            size_t textLength = end - start;
            const size_t declaredLength = data[start];
            if (declaredLength > 0 && declaredLength <= textLength - 1) {
                textStart = start + 1;
                textLength = declaredLength;
            }

            for (size_t position = textStart;
                    position < textStart + textLength && position < data.size();
                    ++position) {
                const uint8_t ch = data[position];
                if (ch == 0) break;
                if (ch >= 0x20 || ch == '\t') {
                    text.push_back(static_cast<char>(ch));
                }
            }
        }

        output << "    {\n      \"index\": " << index
               << ",\n      \"offset\": " << offsets[index]
               << ",\n      \"text\": ";
        writeJsonString(output, text);
        output << ",\n      \"translation\": \"\"\n    }";
        if (index + 1 < entryCount) output << ",";
        output << "\n";
    }

    output << "  ]\n}\n";
    output.close();
    return output.good();
}

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

    std::string nomeMinusculo = nomeFicheiro;
    std::transform(
            nomeMinusculo.begin(),
            nomeMinusculo.end(),
            nomeMinusculo.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    // Texlists compactados são exportados diretamente como JSON; não viram .bin.
    if (extensao == ".prs"
            && pastaDefinitiva.filename() == "textures"
            && nomeMinusculo.find("texlist") != std::string::npos) {
        std::vector<ExtractedTexture> extractedTextures;
        const std::string nomeSemExtensao =
                fs::path(nomeFicheiro).stem().string();
        if (!ExtractPVMContainer(
                    dadosProcessados,
                    extractedTextures,
                    nomeSemExtensao,
                    pastaDefinitiva)) {
            __android_log_print(
                    ANDROID_LOG_ERROR,
                    "SA2B_PIPELINE",
                    "Não foi possível gerar JSON do texlist: %s",
                    nomeFicheiro.c_str());
            return false;
        }

        std::error_code removeError;
        fs::remove(caminhoOriginal, removeError);
        __android_log_print(
                ANDROID_LOG_INFO,
                "SA2B_PIPELINE",
                "Texlist exportado para JSON: %s",
                nomeSemExtensao.c_str());
        return true;
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

        // O PRS é apenas a compressão; se o nome não informa o formato,
        // classifica o payload pelo cabeçalho real.
        if (fs::path(nomeProcessado).extension().empty()) {
            nomeProcessado += inferResourceExtension(dadosProcessados);
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

    const std::string processadaExtension = getFileExtension(nomeProcessado);
    const bool recursoBinario = processadaExtension == ".bin";
    const bool texturaRgbaCrua = !recursoBinario
            && processadaExtension == ".rgba"
            && pastaDefinitiva.filename() == "textures";
    const bool texturaParaConversao =
            pastaDefinitiva.filename() == "textures"
            && (processadaExtension == ".gvm"
                    || processadaExtension == ".pvm"
                    || processadaExtension == ".gvr"
                    || processadaExtension == ".pvr");
    const fs::path pastaDestino = recursoBinario
            ? pastaDefinitiva.parent_path() / "bin" / pastaDefinitiva.filename()
            : (texturaParaConversao ? pastaTemp : pastaDefinitiva);
    const std::string nomeDestino = texturaRgbaCrua
            ? fs::path(nomeProcessado).stem().string() + ".tga"
            : nomeProcessado;
    const fs::path caminhoFinal = pastaDestino / nomeDestino;

    bool saved = false;
    if (caminhoFinal == caminhoTemp) {
        saved = true;
    } else if (texturaRgbaCrua) {
        saved = salvarTGA(caminhoFinal.string(), dadosProcessados, 256, 256);
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

void organizarBinariosParaTraducao(const std::string& baseDirPath) {
    const fs::path raiz(baseDirPath);
    const std::vector<std::string> categorias = {
            "models", "textures", "audio", "misc"
    };

    for (const std::string& categoria : categorias) {
        const fs::path pastaOrigem = raiz / "temp" / categoria;
        const fs::path pastaBin = raiz / "bin" / categoria;
        std::error_code error;
        fs::create_directories(pastaBin, error);
        if (error) {
            __android_log_print(
                    ANDROID_LOG_ERROR,
                    "SA2B_PIPELINE",
                    "Não foi possível criar a pasta bin/%s.",
                    categoria.c_str());
            continue;
        }

        fs::directory_iterator entry(pastaOrigem, error);
        const fs::directory_iterator end;
        if (error) continue;

        while (entry != end) {
            const fs::path caminhoArquivo = entry->path();
            std::error_code statusError;
            if (fs::is_regular_file(caminhoArquivo, statusError)
                    && getFileExtension(caminhoArquivo.filename().string()) == ".bin") {
                const fs::path destino = pastaBin / caminhoArquivo.filename();
                std::error_code copyError;
                fs::copy_file(
                        caminhoArquivo,
                        destino,
                        fs::copy_options::overwrite_existing,
                        copyError);
                if (copyError) {
                    __android_log_print(
                            ANDROID_LOG_ERROR,
                            "SA2B_PIPELINE",
                            "Falha ao colocar arquivo em bin/%s: %s",
                            categoria.c_str(),
                            caminhoArquivo.filename().string().c_str());
                }
            }

            entry.increment(error);
            if (error) break;
        }
    }
}

static void moveLegacyUiFilesToTextures(const std::string& baseDirPath) {
    const fs::path textureDirectory = fs::path(baseDirPath) / "temp/textures";
    const std::vector<fs::path> legacyUiDirectories = {
            fs::path(baseDirPath) / "temp/ui",
            fs::path(baseDirPath) / "bin/ui",
            fs::path(baseDirPath) / "ui"
    };

    std::error_code error;
    fs::create_directories(textureDirectory, error);
    if (error) return;

    for (const fs::path& sourceDirectory : legacyUiDirectories) {
        error.clear();
        fs::directory_iterator entry(sourceDirectory, error);
        if (error) continue;

        const fs::directory_iterator end;
        while (entry != end) {
            const fs::path sourceFile = entry->path();
            std::error_code statusError;
            if (fs::is_regular_file(sourceFile, statusError) && !statusError) {
                fs::path destination = textureDirectory / sourceFile.filename();
                const std::string stem = sourceFile.stem().string();
                const std::string extension = sourceFile.extension().string();
                for (uint32_t suffix = 1; fs::exists(destination, statusError); ++suffix) {
                    statusError.clear();
                    std::string uniqueName = stem;
                    uniqueName += "_ui_";
                    uniqueName += std::to_string(suffix);
                    uniqueName += extension;
                    destination = textureDirectory / uniqueName;
                }

                error.clear();
                fs::rename(sourceFile, destination, error);
                if (error) {
                    error.clear();
                    fs::copy_file(sourceFile, destination, error);
                    if (!error) {
                        std::error_code removeError;
                        fs::remove(sourceFile, removeError);
                    }
                }
            }

            entry.increment(error);
            if (error) break;
        }

        error.clear();
        fs::remove(sourceDirectory, error); // Remove apenas se estiver vazia.
    }
}

void processOnlySupportedFiles(const std::string& baseDirPath) {
    moveLegacyUiFilesToTextures(baseDirPath);

    const std::vector<fs::path> sourceDirectories = {
            fs::path(baseDirPath) / "temp/textures",
            fs::path(baseDirPath) / "bin/textures",
            fs::path(baseDirPath) / "temp/misc",
            fs::path(baseDirPath) / "bin/misc"
    };
    std::vector<fs::path> sourceFiles;

    for (const fs::path& directory : sourceDirectories) {
        std::error_code iterationError;
        fs::directory_iterator entry(directory, iterationError);
        const fs::directory_iterator end;
        if (iterationError) continue;

        while (entry != end) {
            std::error_code statusError;
            if (entry->is_regular_file(statusError)) {
                sourceFiles.push_back(entry->path());
            }
            entry.increment(iterationError);
            if (iterationError) break;
        }
    }

    const fs::path finalTextureDirectory = fs::path(baseDirPath) / "textures";
    for (const fs::path& sourcePath : sourceFiles) {
        std::error_code existsError;
        if (!fs::exists(sourcePath, existsError) || existsError) continue;

        const std::string fileName = sourcePath.filename().string();
        const std::string extension = getFileExtension(fileName);
        std::string lowerName = fileName;
        std::transform(
                lowerName.begin(),
                lowerName.end(),
                lowerName.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        const fs::path tempTextureDirectory =
                fs::path(baseDirPath) / "temp/textures";
        if (extension == ".gvr" || extension == ".pvr") {
            if (sourcePath.parent_path() != tempTextureDirectory) {
                const std::vector<uint8_t> textureData =
                        lerArquivoBinario(sourcePath.string());
                const fs::path stagedTexture =
                        tempTextureDirectory / sourcePath.filename();
                if (!textureData.empty()
                        && extrairParaTemp(textureData, stagedTexture)) {
                    std::error_code removeError;
                    fs::remove(sourcePath, removeError);
                }
            }
            continue;
        }

        const bool isPvm = extension == ".pvm";
        const bool isGvm = extension == ".gvm";
        const bool isTextBankBin =
                extension == ".bin" && lowerName.rfind("text_", 0) == 0;
        const bool isTextureListBin =
                extension == ".bin" && lowerName.find("texlist") != std::string::npos;

        if (isTextBankBin) {
            const std::vector<uint8_t> textBank =
                    lerArquivoBinario(sourcePath.string());
            if (exportTextBankJson(
                        textBank,
                        sourcePath.filename().string(),
                        finalTextureDirectory)) {
                std::error_code removeError;
                fs::remove(sourcePath, removeError);
                for (const fs::path& directory : sourceDirectories) {
                    if (directory == sourcePath.parent_path()) continue;
                    removeError.clear();
                    fs::remove(directory / sourcePath.filename(), removeError);
                }
            }
            continue;
        }

        if (extension == ".bin" && !isTextureListBin) {
            const std::vector<uint8_t> binaryData =
                    lerArquivoBinario(sourcePath.string());
            uint32_t width = 0;
            uint32_t height = 0;
            uint8_t pixelFormat = 0;
            uint8_t dataFormat = 0;
            uint8_t textureFlags = 0;
            bool isGvr = false;
            size_t pixelDataOffset = 0;
            if (readPVRDimensions(
                        binaryData,
                        width,
                        height,
                        pixelFormat,
                        dataFormat,
                        textureFlags,
                        isGvr,
                        pixelDataOffset)) {
                const std::string textureExtension =
                        inferResourceExtension(binaryData);
                const fs::path stagedTexture =
                        fs::path(baseDirPath) / "temp/textures"
                        / (sourcePath.stem().string() + textureExtension);
                if (extrairParaTemp(binaryData, stagedTexture)) {
                    std::error_code removeError;
                    fs::remove(sourcePath, removeError);
                    continue;
                }
            }
        }

        if (!isPvm && !isGvm && !isTextureListBin) {
            __android_log_print(
                    ANDROID_LOG_DEBUG,
                    SUPPORTED_PIPELINE_LOG_TAG,
                    "Ignorando arquivo binário sem cabeçalho de textura: %s",
                    fileName.c_str());
            continue;
        }

        const std::vector<uint8_t> containerData =
                lerArquivoBinario(sourcePath.string());
        std::vector<ExtractedTexture> extractedTextures;
        const bool containerParsed = !containerData.empty()
                && ExtractPVMContainer(
                        containerData,
                        extractedTextures,
                        sourcePath.stem().string(),
                        finalTextureDirectory);
        if (!containerParsed || (isGvm && extractedTextures.empty())) {
            __android_log_print(
                    ANDROID_LOG_ERROR,
                    SUPPORTED_PIPELINE_LOG_TAG,
                    "Falha ao gerar JSON do texlist/container: %s",
                    fileName.c_str());
            continue;
        }

        // Materializa cada GVRT/PVRT na pasta temporária. A etapa
        // processTexturesWithChecks() converte esses arquivos para .tga.
        bool extractedSuccessfully = true;
        for (size_t index = 0; index < extractedTextures.size(); ++index) {
            std::string textureName = fs::path(
                    extractedTextures[index].name).filename().string();
            if (textureName.empty() || textureName == "." || textureName == "..") {
                textureName = "texture_" + std::to_string(index);
            }
            if (fs::path(textureName).extension().empty()) {
                textureName += inferResourceExtension(
                        extractedTextures[index].pvrData);
            }

            // Texturas extraídas de containers (inclusive GVMs em misc)
            // devem entrar no diretório consumido pelo tradutor na mesma execução.
            const fs::path extractedPath =
                    fs::path(baseDirPath) / "temp/textures" / textureName;
            if (!extrairParaTemp(
                        extractedTextures[index].pvrData,
                        extractedPath)) {
                extractedSuccessfully = false;
                __android_log_print(
                        ANDROID_LOG_ERROR,
                        SUPPORTED_PIPELINE_LOG_TAG,
                        "Falha ao materializar textura extraída: %s",
                        extractedPath.string().c_str());
            }
        }

        if (!extractedSuccessfully) continue;

        __android_log_print(
                ANDROID_LOG_INFO,
                SUPPORTED_PIPELINE_LOG_TAG,
                "Container PVM/GVM extraído para conversão RGBA: %s",
                fileName.c_str());

        std::error_code removeError;
        fs::remove(sourcePath, removeError);
        if (removeError) {
            __android_log_print(
                    ANDROID_LOG_WARN,
                    SUPPORTED_PIPELINE_LOG_TAG,
                    "Texturas extraídas, mas não foi possível remover o container: %s",
                    sourcePath.string().c_str());
        }

        if (isTextureListBin) {
            for (const fs::path& directory : sourceDirectories) {
                if (directory == sourcePath.parent_path()) continue;
                removeError.clear();
                fs::remove(directory / sourcePath.filename(), removeError);
            }
        }
    }
}

bool modelFileExists(const std::string& path) {
    struct stat fileStatus;
    return stat(path.c_str(), &fileStatus) == 0;
}

void processModelsWithChecks(
        const std::string& baseDirPath,
        const std::function<void()>& onFileTranslated) {
    const std::string tempModelsPath = baseDirPath + "/temp/models";
    const std::string finalModelsPath = baseDirPath + "/bin/models";

    std::error_code directoryError;
    fs::create_directories(finalModelsPath, directoryError);
    if (directoryError) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                MODEL_PIPELINE_LOG_TAG,
                "Não foi possível criar a pasta de binários: %s",
                finalModelsPath.c_str());
        return;
    }

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
            if (onFileTranslated) onFileTranslated();
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
            if (onFileTranslated) onFileTranslated();
        }
    }

    closedir(dir);
}

