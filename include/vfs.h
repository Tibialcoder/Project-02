#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <android/asset_manager.h>

#ifdef __cplusplus
extern "C" {
#endif

	// Inicializa o gerenciador de assets do Android na inicialização do app
	void vfs_init(AAssetManager* mgr);

	// Funções de substituição HLE para o sistema de arquivos do GameCube (DVD / ARAM)
	int32_t vfs_dvd_open(const char* filename, void* fileInfo);
	int32_t vfs_dvd_read(void* fileInfo, void* dest, int32_t size, int32_t offset);
	int32_t vfs_dvd_close(void* fileInfo);

#ifdef __cplusplus
}
#endif