#include "vfs.h"
#include <android/log.h>
#include <unordered_map>
#include <string>
#include <cstdlib>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SA2B_VFS", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SA2B_VFS", __VA_ARGS__)

static AAssetManager* s_asset_manager = nullptr;

// Estrutura para controlar arquivos abertos no jogo
struct VirtualFile {
    AAsset* asset;
    size_t size;
};

void vfs_init(AAssetManager* mgr) {
    s_asset_manager = mgr;
    LOGI("FileSystem Virtual inicializado com sucesso.");
}

// Intercepta a chamada DVDOpen original do GameCube
int32_t vfs_dvd_open(const char* filename, void* fileInfo) {
    if (!s_asset_manager) {
        LOGE("AAssetManager não foi inicializado!");
        return 0; // Falha
    }

    // Normaliza o caminho para apontar para a pasta assets/files/ do projeto
    std::string asset_path = "files/" + std::string(filename);

    AAsset* asset = AAssetManager_open(s_asset_manager, asset_path.c_str(), AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Arquivo não encontrado nos assets: %s", asset_path.c_str());
        return 0;
    }

    VirtualFile* vfile = new VirtualFile();
    vfile->asset = asset;
    vfile->size = AAsset_getLength(asset);

    // Preenche a estrutura de controle de arquivo esperada pelo jogo (DVDFileInfo)
    // No GameCube, o ponteiro fileInfo armazena internamente os dados de controle
    *(VirtualFile**)fileInfo = vfile;

    LOGI("Arquivo aberto com sucesso via VFS: %s (Tamanho: %zu bytes)", asset_path.c_str(), vfile->size);
    return 1; // Sucesso (True)
}

// Intercepta a chamada DVDRead
int32_t vfs_dvd_read(void* fileInfo, void* dest, int32_t size, int32_t offset) {
    VirtualFile* vfile = *(VirtualFile**)fileInfo;
    if (!vfile || !vfile->asset) return -1;

    // O GameCube usa leitura síncrona. Aqui lemos diretamente do buffer do AAsset.
    const void* buffer = AAsset_getBuffer(vfile->asset);
    if (!buffer) return -1;

    if (offset + size > (int32_t)vfile->size) {
        size = vfile->size - offset; // Limita ao tamanho real do arquivo
    }

    memcpy(dest, (const char*)buffer + offset, size);
    return size;
}

// Intercepta a chamada DVDClose
int32_t vfs_dvd_close(void* fileInfo) {
    VirtualFile* vfile = *(VirtualFile**)fileInfo;
    if (vfile) {
        if (vfile->asset) {
            AAsset_close(vfile->asset);
        }
        delete vfile;
        *(VirtualFile**)fileInfo = nullptr;
    }
    return 0; // 0 = OK
}