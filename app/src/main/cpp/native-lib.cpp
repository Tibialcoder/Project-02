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

#define LOG_TAG "SA2B_Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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

#define THREAD_LOG_TAG "SA2B_THREAD"

void backgroundTranslationTask(std::string baseDir) {
    __android_log_print(
            ANDROID_LOG_INFO,
            THREAD_LOG_TAG,
            "Iniciando tradução em segundo plano...");

    translateAndSaveTextures(baseDir);

    __android_log_print(
            ANDROID_LOG_INFO,
            THREAD_LOG_TAG,
            "Tradução concluída!");
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_sa2b_MainActivity_nativeStartTranslationAsync(
        JNIEnv* env,
        jobject /* thiz */,
        jstring jOutputPath) {
    if (jOutputPath == nullptr) {
        __android_log_print(
                ANDROID_LOG_ERROR,
                THREAD_LOG_TAG,
                "Caminho de saída nulo.");
        return;
    }

    const char* outputDir = env->GetStringUTFChars(jOutputPath, nullptr);
    if (outputDir == nullptr) {
        return;
    }

    std::string baseDir = std::string(outputDir) + "/Project-02";
    env->ReleaseStringUTFChars(jOutputPath, outputDir);

    std::thread worker(backgroundTranslationTask, std::move(baseDir));
    worker.detach();
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

    // 2. Texturas (.pvm, .prs que sejam de textura, ou imagens)
    if (lowerName.find(".pvm") != std::string::npos ||
        lowerName.find("tex") != std::string::npos ||
        lowerName.find("bg") != std::string::npos) {
        return "textures";
    }

    // 3. UI / Menus (arquivos comuns de interface, fontes ou diálogos)
    if (lowerName.find("ui") != std::string::npos ||
        lowerName.find("menu") != std::string::npos ||
        lowerName.find("font") != std::string::npos ||
        lowerName.find("message") != std::string::npos) {
        return "ui";
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

// Função para obter a extensão do arquivo
std::string getFileExtension(const std::string& filename) {
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
    std::string pathUI       = baseDir + "/ui";
    std::string pathMisc     = baseDir + "/misc";

    createDirectory(baseDir);
    createDirectory(pathTemp);
    createDirectory(pathModels);
    createDirectory(pathTextures);
    createDirectory(pathAudio);
    createDirectory(pathUI);
    createDirectory(pathMisc);

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
            else if (category == "ui") targetFolder = pathUI;
            else targetFolder = pathMisc;

            // Isola apenas o nome base do arquivo
            size_t lastSlash = nameStr.find_last_of("/\\");
            std::string baseName = (lastSlash == std::string::npos) ? nameStr : nameStr.substr(lastSlash + 1);

            std::string fullPath = targetFolder + "/" + baseName;

            // Extrai primeiro para temp/<categoria>, preservando o arquivo bruto.
            std::string tempCategoryPath = pathTemp + "/" + category;
            createDirectory(tempCategoryPath);
            std::string tempFilePath = tempCategoryPath + "/" + baseName;
            std::vector<uint8_t> fileBuffer(fileLength);
            fseek(isoFile, fileOffset, SEEK_SET);
            fread(fileBuffer.data(), 1, fileLength, isoFile);

            FILE* tempFile = fopen(tempFilePath.c_str(), "wb");
            if (tempFile) {
                fwrite(fileBuffer.data(), 1, fileLength, tempFile);
                fclose(tempFile);

                // TODO: traduzir o formato SEGA antes de gravar no destino final.
                // Por enquanto, mantém os bytes intactos e organiza por categoria.
                FILE* outFile = fopen(fullPath.c_str(), "wb");
                if (outFile) {
                    fwrite(fileBuffer.data(), 1, fileLength, outFile);
                    fclose(outFile);
                }
            }

            extractedCount++;

            // Informa o Java a cada arquivo extraído
            if (progressMethod) {
                env->CallVoidMethod(thiz, progressMethod, static_cast<jint>(extractedCount), static_cast<jint>(totalEntries));
            }
        }
    }

    fclose(isoFile);

    // Traduz as texturas extraídas da pasta temporária para a pasta final.
    translateTextures(baseDir);

    env->ReleaseStringUTFChars(jOutputDir, outputDir);
    __android_log_print(ANDROID_LOG_INFO, "SA2B_ISO", "Assets organizados e extraídos com sucesso nas pastas!");
    return JNI_TRUE;
}

GLuint loadTextureToGPU(const std::string& textureFilePath, int width, int height) {
    if (width <= 0 || height <= 0) {
        __android_log_print(ANDROID_LOG_ERROR, "SA2B_RENDERER",
                "Dimensões inválidas para textura: %d x %d", width, height);
        return 0;
    }

    std::ifstream file(textureFilePath, std::ios::binary);
    if (!file.is_open()) {
        __android_log_print(ANDROID_LOG_ERROR, "SA2B_RENDERER",
                "Erro ao abrir textura traduzida: %s", textureFilePath.c_str());
        return 0;
    }

    const size_t dataSize = static_cast<size_t>(width)
            * static_cast<size_t>(height) * 4;
    std::vector<uint8_t> rgbaData(dataSize);
    file.read(reinterpret_cast<char*>(rgbaData.data()),
              static_cast<std::streamsize>(rgbaData.size()));

    if (file.gcount() != static_cast<std::streamsize>(rgbaData.size())) {
        __android_log_print(ANDROID_LOG_ERROR, "SA2B_RENDERER",
                "Arquivo de textura incompleto: %s", textureFilePath.c_str());
        return 0;
    }

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) {
        __android_log_print(ANDROID_LOG_ERROR, "SA2B_RENDERER",
                "Falha ao criar textura OpenGL.");
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, rgbaData.data());

    glBindTexture(GL_TEXTURE_2D, 0);
    __android_log_print(ANDROID_LOG_INFO, "SA2B_RENDERER",
            "Textura carregada com sucesso na GPU! ID: %u", textureId);
    return textureId;
}

// Estado do ciclo de teste das texturas traduzidas.
auto lastSwitchTime = std::chrono::steady_clock::now();
int currentTextureIndex = 0;
std::vector<std::string> translatedTextureFiles;
GLuint currentTextureGPUId = 0;

void updateAndRenderTextureLoop() {
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - lastSwitchTime).count();

    if (elapsedSeconds >= 5 && !translatedTextureFiles.empty()) {
        lastSwitchTime = currentTime;
        currentTextureIndex =
                (currentTextureIndex + 1) % translatedTextureFiles.size();

        const std::string& nextTexPath =
                translatedTextureFiles[currentTextureIndex];

        if (currentTextureGPUId != 0) {
            glDeleteTextures(1, &currentTextureGPUId);
            currentTextureGPUId = 0;
        }

        currentTextureGPUId = loadTextureToGPU(nextTexPath, 256, 256);
    }
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
    LOGI("OpenGL ES inicializado com sucesso via nativeInit!");
    // Inicializações de shaders, buffers ou texturas podem ser feitas aqui
}

// 6. Função chamada a cada frame para renderizar e colorir a tela
extern "C" JNIEXPORT void JNICALL
Java_com_example_sa2b_MainActivity_nativeRender(JNIEnv* env, jclass clazz) {
    // Define uma cor sólida na tela (ex: Azul claro) para tirar a tela cinza
    glClearColor(0.2f, 0.6f, 1.0f, 1.0f);

    // Limpa o buffer de cor da tela com a cor definida acima
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}