#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <GLES3/gl3.h>
#include <vector>
#include <string>
#include <unistd.h>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <sys/stat.h>
#include <cerrno>
#include <arpa/inet.h>
#include <android/log.h>
#include <fstream>
#include <dirent.h>
#include <thread>
#include <utility>
#include <cstdint>
#include <chrono>
#include <limits>
#include <random>
#include <filesystem>
#include "prs_decompressor.h"
#include "texture_translation.h"
#include "resource_file_manager.h"
#include "resource_pipeline.h"

namespace fs = std::filesystem;

#define LOG_TAG "SA2B_Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)








#define THREAD_LOG_TAG "SA2B_THREAD"

static JavaVM* g_javaVm = nullptr;

size_t countPendingTranslationFiles(const std::string& baseDir) {
    size_t pending = 0;
    const std::vector<fs::path> directories = {
            fs::path(baseDir) / "temp/models",
            fs::path(baseDir) / "temp/textures"
    };

    for (const fs::path& directory : directories) {
        std::error_code error;
        if (!fs::exists(directory, error) || error) continue;
        for (fs::directory_iterator entry(directory, error), end;
                !error && entry != end; entry.increment(error)) {
            std::error_code statusError;
            if (!entry->is_regular_file(statusError) || statusError) continue;
            const std::string extension =
                    getFileExtension(entry->path().filename().string());
            if (extension == ".gvm" || extension == ".pvm") continue;
            ++pending;
        }
    }
    return pending;
}

size_t countPendingPrsFiles(const std::string& baseDir) {
    size_t pending = 0;
    const std::vector<fs::path> directories = {
            fs::path(baseDir) / "temp",
            fs::path(baseDir) / "temp/models",
            fs::path(baseDir) / "temp/textures",
            fs::path(baseDir) / "temp/audio",
            fs::path(baseDir) / "temp/ui",
            fs::path(baseDir) / "temp/misc"
    };
    for (const fs::path& directory : directories) {
        std::error_code error;
        for (fs::directory_iterator entry(directory, error), end;
                !error && entry != end; entry.increment(error)) {
            std::error_code statusError;
            if (entry->is_regular_file(statusError)
                    && !statusError
                    && getFileExtension(entry->path().filename().string()) == ".prs") {
                ++pending;
            }
        }
    }
    return pending;
}

void notifyTranslationPreparing(
        JNIEnv* env, jobject activity, int prepared, int total) {
    if (env == nullptr || activity == nullptr) return;
    jclass clazz = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(
            clazz, "onTranslationPreparing", "(II)V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method, prepared, total);
    }
    env->DeleteLocalRef(clazz);
}

void notifyTranslationOrganizing(JNIEnv* env, jobject activity) {
    if (env == nullptr || activity == nullptr) return;
    jclass clazz = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(
            clazz, "onTranslationOrganizing", "()V");
    if (method != nullptr) env->CallVoidMethod(activity, method);
    env->DeleteLocalRef(clazz);
}

void notifyTranslationProgress(
        JNIEnv* env,
        jobject activity,
        int translated,
        int total) {
    if (env == nullptr || activity == nullptr) return;
    jclass clazz = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(clazz, "onTranslationProgress", "(II)V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method, translated, total);
    }
    env->DeleteLocalRef(clazz);
}

void notifyTranslationFinished(JNIEnv* env, jobject activity) {
    if (env == nullptr || activity == nullptr) return;
    jclass clazz = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(clazz, "onTranslationFinished", "()V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method);
    }
    env->DeleteLocalRef(clazz);
}

void backgroundTranslationTask(
        std::string baseDir,
        JavaVM* javaVm,
        jobject activity) {
    JNIEnv* env = nullptr;
    bool attached = false;
    if (javaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (javaVm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            env = nullptr;
        } else {
            attached = true;
        }
    }

    // Mostra progresso já durante a descompactação que antecede a tradução.
    const int totalPrs = static_cast<int>(countPendingPrsFiles(baseDir));
    int preparedPrs = 0;
    notifyTranslationPreparing(env, activity, 0, totalPrs);
    processPipelineWithDecompressed(baseDir, [&]() {
        ++preparedPrs;
        notifyTranslationPreparing(env, activity, preparedPrs, totalPrs);
    });

    notifyTranslationOrganizing(env, activity);
    organizarBinariosParaTraducao(baseDir);
    processOnlySupportedFiles(baseDir);

    const size_t totalFiles = countPendingTranslationFiles(baseDir);
    int translatedFiles = 0;

    __android_log_print(
            ANDROID_LOG_INFO,
            THREAD_LOG_TAG,
            "Iniciando conversão de %zu arquivo(s) preparado(s).",
            totalFiles);
    notifyTranslationProgress(env, activity, 0, static_cast<int>(totalFiles));

    const auto reportFileTranslated = [&]() {
        ++translatedFiles;
        notifyTranslationProgress(
                env,
                activity,
                translatedFiles,
                static_cast<int>(totalFiles));
    };

    processTexturesWithChecks(baseDir, reportFileTranslated);
    processModelsWithChecks(baseDir, reportFileTranslated);
    notifyTranslationFinished(env, activity);

    __android_log_print(
            ANDROID_LOG_INFO,
            THREAD_LOG_TAG,
            "Tradução concluída!");

    if (javaVm != nullptr) {
        env->DeleteGlobalRef(activity);
        if (attached) javaVm->DetachCurrentThread();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_sa2b_MainActivity_nativeStartTranslationAsync(
        JNIEnv* env,
        jobject thiz,
        jstring jOutputPath) {
    if (jOutputPath == nullptr) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                THREAD_LOG_TAG,
                "Caminho de saída nulo.");
        return;
    }

    const char* outputDir = env->GetStringUTFChars(jOutputPath, nullptr);
    if (outputDir == nullptr) return;

    std::string baseDir(outputDir);
    env->ReleaseStringUTFChars(jOutputPath, outputDir);
    env->GetJavaVM(&g_javaVm);

    jobject activity = env->NewGlobalRef(thiz);
    std::thread worker(
            backgroundTranslationTask,
            std::move(baseDir),
            g_javaVm,
            activity);
    worker.detach();
}

#define AUTO_PIPELINE_LOG_TAG "SA2B_AUTO_PIPELINE"

static bool hasPendingTranslationFiles(const std::string& baseDir) {
    const std::vector<fs::path> directories = {
            fs::path(baseDir) / "temp/models",
            fs::path(baseDir) / "temp/textures",
            fs::path(baseDir) / "temp/audio",
            fs::path(baseDir) / "temp/ui",
            fs::path(baseDir) / "temp/misc",
            fs::path(baseDir) / "bin/textures",
            fs::path(baseDir) / "bin/ui",
            fs::path(baseDir) / "bin/misc"
    };

    for (const fs::path& directory : directories) {
        std::error_code error;
        if (!fs::exists(directory, error) || error) continue;
        for (const fs::directory_entry& entry :
                fs::directory_iterator(directory, error)) {
            if (error) break;
            if (!entry.is_regular_file(error)) continue;

            const std::string extension =
                    getFileExtension(entry.path().filename().string());
            if (extension == ".prs"
                    || extension == ".gvm"
                    || extension == ".pvm"
                    || extension == ".gvr"
                    || extension == ".pvr"
                    || (extension == ".bin"
                            && directory.parent_path().filename() == "bin"
                            && (directory.filename() == "textures"
                                    || directory.filename() == "misc"))) {
                return true;
            }
        }
    }
    return false;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_sa2b_MainActivity_nativeHasPendingTranslations(
        JNIEnv* env,
        jobject /* thiz */,
        jstring jBasePath) {
    if (jBasePath == nullptr) return JNI_FALSE;

    const char* pathChars = env->GetStringUTFChars(jBasePath, nullptr);
    if (pathChars == nullptr) return JNI_FALSE;

    const bool pending = hasPendingTranslationFiles(pathChars);
    env->ReleaseStringUTFChars(jBasePath, pathChars);
    return pending ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_sa2b_MainActivity_nativeInitPipeline(
        JNIEnv* env,
        jobject /* thiz */,
        jstring jBasePath) {
    if (jBasePath == nullptr) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                AUTO_PIPELINE_LOG_TAG,
                "Caminho base nulo; pipeline não iniciado.");
        return;
    }
    const char* pathChars = env->GetStringUTFChars(jBasePath, nullptr);
    if (pathChars == nullptr) {
        return;
    }

    const std::string baseDirPath(pathChars);
    env->ReleaseStringUTFChars(jBasePath, pathChars);

    createDecompressedDirectory(baseDirPath);

    __android_log_print(
            ANDROID_LOG_INFO,
            AUTO_PIPELINE_LOG_TAG,
            "Iniciando verificação de arquivos pendentes no boot...");

    processPipelineWithDecompressed(baseDirPath);
    organizarBinariosParaTraducao(baseDirPath);
    processOnlySupportedFiles(baseDirPath);
    processTexturesWithChecks(baseDirPath);
    processModelsWithChecks(baseDirPath);

    __android_log_print(
            ANDROID_LOG_INFO,
            AUTO_PIPELINE_LOG_TAG,
            "Pipeline de inicialização concluído.");
}

// Dados básicos de uma malha SA2/Ninja prontos para uso pelo renderer.
struct Vertex3D {
    float x;
    float y;
    float z;
};

struct MeshData {
    std::string name;
    std::vector<Vertex3D> vertices;
};

// Parser inicial para arquivos .sa2/.nj.
// A leitura específica dos chunks Ninja pode ser expandida sem alterar a API.
MeshData parseSA2Model(const std::string& filePath) {
    MeshData mesh;
    mesh.name = filePath;

    FILE* file = std::fopen(filePath.c_str(), "rb");
    if (!file) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                "SA2B_PARSER",
                "Erro ao abrir o arquivo: %s",
                filePath.c_str());
        return mesh;
    }

    // O formato Ninja/SA2 possui chunks e offsets específicos por versão.
    // Mantemos a malha vazia até que o bloco de vértices seja identificado.
    std::fclose(file);
    return mesh;
}

#pragma pack(push, 1)
struct GameCubeHeader {
    char gameCode[4];         // 0x00: Ex: "GSOE" para SA2B
    char makerCode[2];        // 0x04: Ex: "01" (SEGA)
    uint8_t discId;           // 0x06
    uint8_t version;          // 0x07
    uint8_t audioStreaming;   // 0x08
    uint8_t streamBufSize;    // 0x09
    uint8_t unused[22];       // 0x0A - 0x1F
    uint32_t magicWord;       // 0x18: Deve ser obrigatoriamente 0xC2339F3D
    char gameName[960];       // 0x1C ...
    uint32_t debugMonitorOffset; // 0x400
    uint32_t debugMonitorAddress;
    uint32_t unused2[2];
    uint32_t fstOffset;       // 0x424: Offset onde a File System Table (FST) começa na ISO
    uint32_t fstSize;         // 0x428: Tamanho da FST
    uint32_t fstMaxSize;      // 0x42C
};

struct FSTEntry {
    uint32_t nameOffsetAndType; // Byte mais alto define se é diretório, os outros 3 bytes são o offset do nome
    uint32_t fileOffset;        // Offset do arquivo na ISO
    uint32_t fileLength;        // Tamanho do arquivo em bytes (ou total de entradas se for o root)
};

#pragma pack(pop)

// Função auxiliar para ler 4 bytes em Big-Endian e converter para o Android (Little-Endian)
// Função para ler 4 bytes em Big-Endian (padrão GameCube)
uint32_t readBigEndian32(const uint8_t* ptr) {
    return ((uint32_t)ptr[0] << 24) |
           ((uint32_t)ptr[1] << 16) |
           ((uint32_t)ptr[2] << 8)  |
           (uint32_t)ptr[3];
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_sa2b_MainActivity_nativeLoadISO(JNIEnv* env, jclass clazz, jint fileDescriptor) {
    int dupFd = dup(fileDescriptor);
    if (dupFd < 0) return;

    FILE* isoFile = fdopen(dupFd, "rb");
    if (!isoFile) {
        close(dupFd);
        return;
    }

    // Lê os primeiros 2048 bytes do cabeçalho
    std::vector<uint8_t> headerBuffer(2048);
    fseek(isoFile, 0, SEEK_SET);
    fread(headerBuffer.data(), 1, 2048, isoFile);

    // No padrão GameCube, o FST Offset está em 0x424 e o FST Size em 0x428
    uint32_t fstOffset = readBigEndian32(headerBuffer.data() + 0x424);
    uint32_t fstSize   = readBigEndian32(headerBuffer.data() + 0x428);

    __android_log_print(ANDROID_LOG_INFO, "SA2B_ISO", "Header Oficial -> FST Offset: 0x%X | FST Size: %u bytes", fstOffset, fstSize);

    // Validação de segurança para garantir que os valores lidos são coerentes
    if (fstOffset == 0 || fstSize == 0 || fstSize > 10 * 1024 * 1024) {
        __android_log_print(ANDROID_LOG_ERROR, "SA2B_ISO", "Erro: Os valores oficiais da FST parecem inválidos.");
        fclose(isoFile);
        return;
    }

    // Lê a FST real usando o offset oficial
    std::vector<uint8_t> fstData(fstSize);
    fseek(isoFile, fstOffset, SEEK_SET);
    size_t bytesRead = fread(fstData.data(), 1, fstSize, isoFile);

    if (bytesRead < fstSize) {
        __android_log_print(ANDROID_LOG_ERROR, "SA2B_ISO", "Erro ao ler os dados da FST.");
        fclose(isoFile);
        return;
    }

    // O fileLength da raiz (entrada 0) guarda o total de entradas
    uint32_t totalEntries = readBigEndian32(fstData.data() + 8);

    __android_log_print(ANDROID_LOG_INFO, "SA2B_ISO", "SUCESSO! Total real de entradas na FST: %u", totalEntries);

    // A String Table começa logo após todas as entradas (totalEntries * 12 bytes)
    const uint8_t* stringTable = fstData.data() + (totalEntries * 12);

    // Lista os primeiros 30 arquivos
    for (uint32_t i = 0; i < totalEntries && i < 30; ++i) {
        const uint8_t* entryPtr = fstData.data() + (i * 12);

        uint32_t rawNameInfo = readBigEndian32(entryPtr + 0);
        uint32_t fileOffset  = readBigEndian32(entryPtr + 4);
        uint32_t fileLength  = readBigEndian32(entryPtr + 8);

        uint32_t nameOffset = rawNameInfo & 0x00FFFFFF;
        bool isDirectory = (rawNameInfo >> 24) != 0;

        const char* fileName = reinterpret_cast<const char*>(stringTable + nameOffset);

        if (isDirectory) {
            __android_log_print(ANDROID_LOG_INFO, "SA2B_ISO", "[DIR] %s", fileName);
        } else {
            __android_log_print(ANDROID_LOG_INFO, "SA2B_ISO", "[ARQ] %s | Offset: 0x%X | Tamanho: %u bytes",
                                fileName, fileOffset, fileLength);
        }
    }

    fclose(isoFile);
}

// Função para buscar e extrair um arquivo da ISO pelo nome
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_sa2b_MainActivity_nativeExtractFile(JNIEnv* env, jobject thiz, jint fileDescriptor, jstring jFileName, jstring jOutputPath) {
    // Converte o nome do arquivo de Java String para C-string
    const char* targetFileName = env->GetStringUTFChars(jFileName, nullptr);
    const char* outputPath = env->GetStringUTFChars(jOutputPath, nullptr);

    int dupFd = dup(fileDescriptor);
    if (dupFd < 0) {
        env->ReleaseStringUTFChars(jFileName, targetFileName);
        env->ReleaseStringUTFChars(jOutputPath, outputPath);
        return JNI_FALSE;
    }

    FILE* isoFile = fdopen(dupFd, "rb");
    if (!isoFile) {
        close(dupFd);
        env->ReleaseStringUTFChars(jFileName, targetFileName);
        env->ReleaseStringUTFChars(jOutputPath, outputPath);
        return JNI_FALSE;
    }

    // 1. Lê o cabeçalho para pegar a FST
    std::vector<uint8_t> headerBuffer(2048);
    fseek(isoFile, 0, SEEK_SET);
    fread(headerBuffer.data(), 1, 2048, isoFile);

    uint32_t fstOffset = readBigEndian32(headerBuffer.data() + 0x424);
    uint32_t fstSize   = readBigEndian32(headerBuffer.data() + 0x428);

    std::vector<uint8_t> fstData(fstSize);
    fseek(isoFile, fstOffset, SEEK_SET);
    fread(fstData.data(), 1, fstSize, isoFile);

    uint32_t totalEntries = readBigEndian32(fstData.data() + 8);
    const uint8_t* stringTable = fstData.data() + (totalEntries * 12);

    bool found = false;
    uint32_t targetOffset = 0;
    uint32_t targetLength = 0;

    // 2. Varre a FST procurando o arquivo desejado pelo nome
    for (uint32_t i = 0; i < totalEntries; ++i) {
        const uint8_t* entryPtr = fstData.data() + (i * 12);

        uint32_t rawNameInfo = readBigEndian32(entryPtr + 0);
        uint32_t fileOffset  = readBigEndian32(entryPtr + 4);
        uint32_t fileLength  = readBigEndian32(entryPtr + 8);

        uint32_t nameOffset = rawNameInfo & 0x00FFFFFF;
        bool isDirectory = (rawNameInfo >> 24) != 0;

        if (!isDirectory) {
            const char* fileName = reinterpret_cast<const char*>(stringTable + nameOffset);
            if (std::string(fileName) == targetFileName) {
                targetOffset = fileOffset;
                targetLength = fileLength;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        __android_log_print(ANDROID_LOG_ERROR, "SA2B_ISO", "Arquivo não encontrado na ISO: %s", targetFileName);
        fclose(isoFile);
        env->ReleaseStringUTFChars(jFileName, targetFileName);
        env->ReleaseStringUTFChars(jOutputPath, outputPath);
        return JNI_FALSE;
    }

    __android_log_print(ANDROID_LOG_INFO, "SA2B_ISO", "Extraindo %s (Offset: 0x%X, Tamanho: %u)", targetFileName, targetOffset, targetLength);

    // 3. Lê os dados binários do arquivo de dentro da ISO
    std::vector<uint8_t> fileBuffer(targetLength);
    fseek(isoFile, targetOffset, SEEK_SET);
    fread(fileBuffer.data(), 1, targetLength, isoFile);
    fclose(isoFile);

    // 4. Salva o arquivo extraído no diretório interno do aplicativo no Android
    FILE* outFile = fopen(outputPath, "wb");
    if (outFile) {
        fwrite(fileBuffer.data(), 1, targetLength, outFile);
        fclose(outFile);
        __android_log_print(ANDROID_LOG_INFO, "SA2B_ISO", "Arquivo salvo com sucesso em: %s", outputPath);
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "SA2B_ISO", "Erro ao criar o arquivo de saída.");
        found = false;
    }

    env->ReleaseStringUTFChars(jFileName, targetFileName);
    env->ReleaseStringUTFChars(jOutputPath, outputPath);
    return found ? JNI_TRUE : JNI_FALSE;
}

// Função auxiliar para criar diretórios recursivamente no Android




std::string getFileExtensionWithoutDot(const std::string& filename) {
    size_t idx = filename.rfind('.');
    if (idx == std::string::npos) return "";
    std::string ext = filename.substr(idx + 1);
    // Converte para minúsculas para facilitar a comparação
    for (char& c : ext) c = tolower(c);
    return ext;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_sa2b_MainActivity_nativeExtractAll(JNIEnv* env, jobject thiz, jint fileDescriptor, jstring jOutputDir) {
    const char* outputDir = env->GetStringUTFChars(jOutputDir, nullptr);

    int dupFd = dup(fileDescriptor);
    if (dupFd < 0) {
        env->ReleaseStringUTFChars(jOutputDir, outputDir);
        return JNI_FALSE;
    }

    FILE* isoFile = fdopen(dupFd, "rb");
    if (!isoFile) {
        close(dupFd);
        env->ReleaseStringUTFChars(jOutputDir, outputDir);
        return JNI_FALSE;
    }

    // Cria a estrutura base de extração, incluindo a pasta temporária.
    // outputDir já aponta para Documents/Project-02 neste projeto.
    std::string baseDir = std::string(outputDir);
    std::string pathTemp     = baseDir + "/temp";
    std::string pathModels   = baseDir + "/models";
    std::string pathTextures = baseDir + "/textures";
    std::string pathAudio    = baseDir + "/audio";
    std::string pathMisc     = baseDir + "/misc";
    std::string pathBin      = baseDir + "/bin";

    createDirectory(baseDir);
    createDirectory(pathTemp);
    createDirectory(pathModels);
    createDirectory(pathTextures);
    createDirectory(pathAudio);
    createDirectory(pathMisc);
    createDirectory(pathBin);
    createDirectory(pathBin + "/models");
    createDirectory(pathBin + "/textures");
    createDirectory(pathBin + "/audio");
    createDirectory(pathBin + "/misc");

    // Lê o cabeçalho e a FST
    std::vector<uint8_t> headerBuffer(2048);
    fseek(isoFile, 0, SEEK_SET);
    fread(headerBuffer.data(), 1, 2048, isoFile);

    uint32_t fstOffset = readBigEndian32(headerBuffer.data() + 0x424);
    uint32_t fstSize   = readBigEndian32(headerBuffer.data() + 0x428);

    std::vector<uint8_t> fstData(fstSize);
    fseek(isoFile, fstOffset, SEEK_SET);
    fread(fstData.data(), 1, fstSize, isoFile);

    uint32_t totalEntries = readBigEndian32(fstData.data() + 8);
    const uint8_t* stringTable = fstData.data() + (totalEntries * 12);

    // Obtém a referência para chamar o método Java "onExtractionProgress"
    jclass clazz = env->GetObjectClass(thiz);
    jmethodID progressMethod = env->GetMethodID(clazz, "onExtractionProgress", "(II)V");

    uint32_t extractedCount = 0;

    // Varre todas as entradas da FST
    for (uint32_t i = 0; i < totalEntries; ++i) {
        const uint8_t* entryPtr = fstData.data() + (i * 12);

        uint32_t rawNameInfo = readBigEndian32(entryPtr + 0);
        uint32_t fileOffset  = readBigEndian32(entryPtr + 4);
        uint32_t fileLength  = readBigEndian32(entryPtr + 8);

        uint32_t nameOffset = rawNameInfo & 0x00FFFFFF;
        bool isDirectory = (rawNameInfo >> 24) != 0;

        const char* fileName = reinterpret_cast<const char*>(stringTable + nameOffset);

        if (std::string(fileName) == "." || std::string(fileName) == "..") continue;

        if (!isDirectory) {
            std::string nameStr(fileName);

            // Descobre para qual pasta o arquivo deve ir
            std::string category = classifyFile(nameStr);

            std::string targetFolder;
            if (category == "models") targetFolder = pathModels;
            else if (category == "textures") targetFolder = pathTextures;
            else if (category == "audio") targetFolder = pathAudio;
            else targetFolder = pathMisc;

            // Isola apenas o nome base do arquivo
            size_t lastSlash = nameStr.find_last_of("/\\");
            std::string baseName = (lastSlash == std::string::npos) ? nameStr : nameStr.substr(lastSlash + 1);

            // Todo arquivo é gravado na área temporária antes de seguir ao destino.
            const fs::path tempFilePath =
                    fs::path(pathTemp) / category / baseName;
            std::vector<uint8_t> fileBuffer(fileLength);
            fseek(isoFile, fileOffset, SEEK_SET);
            const size_t extractedBytes =
                    fread(fileBuffer.data(), 1, fileLength, isoFile);
            if (extractedBytes != fileLength
                    || !extrairParaTemp(fileBuffer, tempFilePath)) {
                __android_log_print(
                        ANDROID_LOG_ERROR,
                        "SA2B_ISO",
                        "Falha ao extrair para temp: %s",
                        baseName.c_str());
                continue;
            }

            extractedCount++;

            // Informa o Java a cada arquivo extraído
            if (progressMethod) {
                env->CallVoidMethod(thiz, progressMethod, static_cast<jint>(extractedCount), static_cast<jint>(totalEntries));
            }
        }
    }

    fclose(isoFile);

    // A extração termina aqui. A tradução é iniciada pela Activity
    // para que a UI possa trocar de tela e acompanhar o progresso.

    env->ReleaseStringUTFChars(jOutputDir, outputDir);
    __android_log_print(ANDROID_LOG_INFO, "SA2B_ISO", "Assets organizados e extraídos com sucesso nas pastas!");
    return JNI_TRUE;
}

GLuint loadTextureToGPU(const std::string& textureFilePath) {
    std::ifstream file(textureFilePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return 0;
    const std::streamsize fileSize = file.tellg();
    if (fileSize < 18) return 0;
    file.seekg(0, std::ios::beg);

    uint8_t header[18] = {};
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header))) return 0;
    const uint16_t width = static_cast<uint16_t>(header[12])
            | (static_cast<uint16_t>(header[13]) << 8u);
    const uint16_t height = static_cast<uint16_t>(header[14])
            | (static_cast<uint16_t>(header[15]) << 8u);
    const uint8_t bitsPerPixel = header[16];
    const size_t bytesPerPixel = bitsPerPixel / 8u;
    if (header[1] != 0 || header[2] != 2
            || (bitsPerPixel != 24 && bitsPerPixel != 32)
            || width == 0 || height == 0) {
        __android_log_print(ANDROID_LOG_WARN, "SA2B_RENDERER",
                "TGA não suportado: %s", textureFilePath.c_str());
        return 0;
    }

    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (pixelCount > (std::numeric_limits<size_t>::max() / 4u)) return 0;
    const size_t sourceSize = pixelCount * bytesPerPixel;
    const size_t dataOffset = 18u + header[0];
    if (dataOffset > static_cast<size_t>(fileSize)
            || sourceSize > static_cast<size_t>(fileSize) - dataOffset) return 0;

    file.seekg(static_cast<std::streamoff>(dataOffset), std::ios::beg);
    std::vector<uint8_t> sourcePixels(sourceSize);
    if (!file.read(reinterpret_cast<char*>(sourcePixels.data()),
                   static_cast<std::streamsize>(sourceSize))) return 0;

    std::vector<uint8_t> rgbaPixels(pixelCount * 4u);
    const bool topOrigin = (header[17] & 0x20u) != 0;
    const bool rightOrigin = (header[17] & 0x10u) != 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t sourceX = rightOrigin ? width - 1u - x : x;
            const uint32_t sourceY = topOrigin ? y : height - 1u - y;
            const size_t sourceIndex =
                    (static_cast<size_t>(sourceY) * width + sourceX) * bytesPerPixel;
            const size_t targetIndex =
                    (static_cast<size_t>(y) * width + x) * 4u;
            rgbaPixels[targetIndex] = sourcePixels[sourceIndex + 2u];
            rgbaPixels[targetIndex + 1u] = sourcePixels[sourceIndex + 1u];
            rgbaPixels[targetIndex + 2u] = sourcePixels[sourceIndex];
            rgbaPixels[targetIndex + 3u] =
                    bytesPerPixel == 4u ? sourcePixels[sourceIndex + 3u] : 255u;
        }
    }

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) return 0;
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    __android_log_print(ANDROID_LOG_INFO, "SA2B_RENDERER",
            "TGA %s carregado (%ux%u), ID OpenGL %u",
            textureFilePath.c_str(), width, height, textureId);
    return textureId;
}

auto lastSwitchTime = std::chrono::steady_clock::now();
int currentTextureIndex = 0;
std::vector<std::string> translatedTextureFiles;
GLuint currentTextureGPUId = 0;
GLuint textureTestProgram = 0;

static GLuint compileTextureShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void initializeTextureRenderTest() {
    translatedTextureFiles.clear();
    const fs::path textureDirectory =
            "/storage/emulated/0/Documents/Project-02/textures";
    std::error_code error;
    for (fs::directory_iterator entry(textureDirectory, error), end;
            !error && entry != end; entry.increment(error)) {
        if (entry->is_regular_file(error)
                && getFileExtension(entry->path().filename().string()) == ".tga") {
            translatedTextureFiles.push_back(entry->path().string());
        }
    }
    if (translatedTextureFiles.empty()) {
        __android_log_print(ANDROID_LOG_WARN, "SA2B_RENDERER",
                "Nenhum arquivo .tga encontrado em %s",
                textureDirectory.string().c_str());
        return;
    }

    static const char* vertexSource =
            "attribute vec2 aPosition; attribute vec2 aTexCoord; "
            "varying vec2 vTexCoord; void main(){ "
            "gl_Position=vec4(aPosition,0.0,1.0); vTexCoord=aTexCoord; }";
    static const char* fragmentSource =
            "precision mediump float; varying vec2 vTexCoord; "
            "uniform sampler2D uTexture; void main(){ "
            "gl_FragColor=texture2D(uTexture,vTexCoord); }";
    const GLuint vertexShader =
            compileTextureShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader =
            compileTextureShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0) return;

    textureTestProgram = glCreateProgram();
    glAttachShader(textureTestProgram, vertexShader);
    glAttachShader(textureTestProgram, fragmentShader);
    glLinkProgram(textureTestProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    GLint linked = GL_FALSE;
    glGetProgramiv(textureTestProgram, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        glDeleteProgram(textureTestProgram);
        textureTestProgram = 0;
        return;
    }

    static std::mt19937 randomEngine(std::random_device{}());
    std::shuffle(
            translatedTextureFiles.begin(),
            translatedTextureFiles.end(),
            randomEngine);
    currentTextureIndex = 0;
    currentTextureGPUId = loadTextureToGPU(translatedTextureFiles.front());
    lastSwitchTime = std::chrono::steady_clock::now();
}

static void updateTextureTest() {
    if (translatedTextureFiles.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(
                now - lastSwitchTime).count() < 5) return;
    lastSwitchTime = now;
    if (currentTextureIndex + 1 >= translatedTextureFiles.size()) {
        const std::string previousTexture =
                translatedTextureFiles[currentTextureIndex];
        static std::mt19937 randomEngine(std::random_device{}());
        std::shuffle(
                translatedTextureFiles.begin(),
                translatedTextureFiles.end(),
                randomEngine);
        if (translatedTextureFiles.size() > 1
                && translatedTextureFiles.front() == previousTexture) {
            std::swap(translatedTextureFiles.front(), translatedTextureFiles[1]);
        }
        currentTextureIndex = 0;
    } else {
        ++currentTextureIndex;
    }
    if (currentTextureGPUId != 0) glDeleteTextures(1, &currentTextureGPUId);
    currentTextureGPUId = loadTextureToGPU(
            translatedTextureFiles[currentTextureIndex]);
}

static void renderTextureTest() {
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    updateTextureTest();
    if (textureTestProgram == 0 || currentTextureGPUId == 0) return;

    static const GLfloat vertices[] = {
            -1.f, -1.f, 0.f, 1.f,
             1.f, -1.f, 1.f, 1.f,
            -1.f,  1.f, 0.f, 0.f,
             1.f,  1.f, 1.f, 0.f
    };
    glUseProgram(textureTestProgram);
    const GLint position = glGetAttribLocation(textureTestProgram, "aPosition");
    const GLint texCoord = glGetAttribLocation(textureTestProgram, "aTexCoord");
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(static_cast<GLuint>(position));
    glEnableVertexAttribArray(static_cast<GLuint>(texCoord));
    glVertexAttribPointer(static_cast<GLuint>(position), 2, GL_FLOAT,
            GL_FALSE, 4 * sizeof(GLfloat), vertices);
    glVertexAttribPointer(static_cast<GLuint>(texCoord), 2, GL_FLOAT,
            GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, currentTextureGPUId);
    glUniform1i(glGetUniformLocation(textureTestProgram, "uTexture"), 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(static_cast<GLuint>(position));
    glDisableVertexAttribArray(static_cast<GLuint>(texCoord));
    glBindTexture(GL_TEXTURE_2D, 0);
}


GLuint CreateTestTexture() {
    int width = 256;
    int height = 256;
    std::vector<uint8_t> pixels(width * height * 4);

    // Gera um padrão simples de teste (ex: degradê ou quadriculado)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int index = (y * width + x) * 4;
            pixels[index + 0] = (x / 32 % 2 == 0) ? 255 : 0;   // Vermelho
            pixels[index + 1] = (y / 32 % 2 == 0) ? 255 : 0;   // Verde
            pixels[index + 2] = 128;                           // Azul
            pixels[index + 3] = 255;                           // Alpha (Opaco)
        }
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // LOG PARA VERIFICAR SE FUNCIONOU
    if (textureID != 0) {
        LOGI("SUCESSO: Textura criada na GPU com o ID: %u", textureID);
    } else {
        LOGE("ERRO: O OpenGL falhou ao criar a textura!");
    }

    return textureID;
}

// Ponteiro global para ler arquivos de dentro do APK
static AAssetManager* g_assetManager = nullptr;

// 1. Recebe o AssetManager vindo do Java/Kotlin
extern "C" JNIEXPORT void JNICALL
Java_com_example_sa2b_MainActivity_initAssetManager(JNIEnv* env, jclass clazz, jobject assetManager) {
    g_assetManager = AAssetManager_fromJava(env, assetManager);
    if (g_assetManager) {
        LOGI("AAssetManager inicializado com sucesso!");
    } else {
        LOGE("Erro: AAssetManager veio nulo!");
    }
}

// Função para carregar e ler os bytes de um arquivo PVM da APK
bool LoadPVMFromAssets(const char* filename, std::vector<uint8_t>& outData) {
    if (g_assetManager == nullptr) {
        LOGE("AAssetManager é nulo. Chame initAssetManager primeiro.");
        return false;
    }

    AAsset* asset = AAssetManager_open(g_assetManager, filename, AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        LOGE("Não foi possível abrir o arquivo PVM: %s", filename);
        return false;
    }

    off_t fileLength = AAsset_getLength(asset);
    outData.resize(fileLength);

    int bytesRead = AAsset_read(asset, outData.data(), fileLength);
    AAsset_close(asset);

    if (bytesRead <= 0) {
        LOGE("Erro ao ler os dados do PVM: %s", filename);
        return false;
    }

    LOGI("Arquivo PVM carregado com sucesso! Tamanho: %ld bytes", (long)fileLength);
    return true;
}

// 2. Função que lê qualquer arquivo da pasta app/src/main/assets/
bool loadGameAsset(const char* relativePath, std::vector<uint8_t>& outData) {
    if (!g_assetManager) {
        LOGE("AAssetManager nao foi inicializado!");
        return false;
    }

    AAsset* asset = AAssetManager_open(g_assetManager, relativePath, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Arquivo nao encontrado em assets/: %s", relativePath);
        return false;
    }

    off_t fileLength = AAsset_getLength(asset);
    const void* fileBuffer = AAsset_getBuffer(asset);

    if (fileBuffer) {
        outData.resize(fileLength);
        memcpy(outData.data(), fileBuffer, fileLength);
        LOGI("Asset '%s' carregado (%ld bytes)", relativePath, (long)fileLength);
    } else {
        LOGE("Falha ao ler buffer do arquivo: %s", relativePath);
        AAsset_close(asset);
        return false;
    }

    AAsset_close(asset);
    return true;
}

// 3. Cria uma textura no OpenGL ES 3.0 a partir dos pixels decodificados
GLuint createGLTexture(int width, int height, const void* pixelData) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);

    glBindTexture(GL_TEXTURE_2D, 0);
    return textureId;
}

// 4. Carrega arquivo PVM do APK e prepara textura
bool loadPVMTextureFromAssets(const char* filename) {
    std::vector<uint8_t> fileData;
    if (!loadGameAsset(filename, fileData)) {
        return false;
    }

    LOGI("Processando container PVM: %s", filename);

    int width = 256;
    int height = 256;
    std::vector<uint8_t> pixels(width * height * 4, 255);

    GLuint texID = createGLTexture(width, height, pixels.data());
    LOGI("Textura registrada no OpenGL com ID: %d", texID);

    return true;
}

// 5. Função chamada quando a superfície OpenGL é criada
extern "C" JNIEXPORT void JNICALL
Java_com_example_sa2b_MainActivity_nativeInit(JNIEnv* env, jclass clazz) {
    LOGI("Inicializando teste de renderização das texturas TGA.");
    initializeTextureRenderTest();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_sa2b_MainActivity_nativeRender(JNIEnv* env, jclass clazz) {
    renderTextureTest();
}