#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

	// Alocação de Memória do Sistema (Heap do GameCube)
	void* OSAlloc(int32_t size);
	void  OSFree(void* ptr);

	// Gerenciamento de Tempo
	OSTime OSGetTime(void);
	int64_t OSGetTick(void);

	// Stubs de Threads do GameCube
	typedef struct OSThread OSThread;
	int32_t OSCreateThread(OSThread* thread, void* (*routine)(void*), void* param, void* stack, uint32_t stackSize, int32_t priority, uint16_t flags);
	int32_t OSResumeThread(OSThread* thread);
	void    OSSuspendThread(OSThread* thread);

#ifdef __cplusplus
}
#endif