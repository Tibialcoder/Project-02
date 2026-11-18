#include "dolphin_os.h"
#include "ppc_context.h"
#include <android/log.h>
#include <cstdlib>
#include <thread>
#include <chrono>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SA2B_OS", __VA_ARGS__)

// Estrutura interna de controle de Thread compatível com o layout do GameCube
struct OSThread {
    uint32_t context[64]; // Espaço para salvar registradores da thread
    std::thread* std_thread;
    void* (*entry_point)(void*);
    void* param;
};

// 1. Gerenciamento de Heap / Alocação de Memória
void* OSAlloc(int32_t size) {
    if (size <= 0) return nullptr;
    // Aloca memória alinhada a 32 bytes (padrão exigido por várias rotinas do GameCube)
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 32, size) != 0) {
        return nullptr;
    }
    return ptr;
}

void OSFree(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}

// 2. Funções de Tempo e Clock do Sistema
OSTime OSGetTime(void) {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

int64_t OSGetTick(void) {
    // Retorna ticks baseados no clock padrão simulado
    return (int64_t)(OSGetTime() * 40500000ULL / 1000000ULL); // ~40.5 MHz (Clock do Gekko)
}

// 3. Stubs de Threads
int32_t OSCreateThread(OSThread* thread, void* (*routine)(void*), void* param, void* stack, uint32_t stackSize, int32_t priority, uint16_t flags) {
    if (!thread) return 0;
    thread->entry_point = routine;
    thread->param = param;
    thread->std_thread = nullptr;
    LOGI("OSCreateThread criado com sucesso.");
    return 1; // 1 = True / Sucesso
}

int32_t OSResumeThread(OSThread* thread) {
    if (!thread || thread->std_thread) return 0;

    // Inicia a thread real usando C++ std::thread rodando no Android
    thread->std_thread = new std::thread([thread]() {
        if (thread->entry_point) {
            thread->entry_point(thread->param);
        }
        });

    LOGI("OSResumeThread executado: Thread iniciada.");
    return 1;
}

void OSSuspendThread(OSThread* thread) {
    // Suspensão completa exigiria primitivas complexas de interrupção, 
    // mas na maioria dos ports estáticos as threads principais rodam livres.
}