#include <SDL.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <cstdlib>
#include <cstring>
#include "ppc_context.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SA2B_RECOMP", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SA2B_RECOMP", __VA_ARGS__)

uint8_t* g_gc_ram = nullptr;
PPCContext g_ppc_ctx;

// Assinatura da função principal recompilada do main.dol (GameCube)
extern "C" void gc_game_main(PPCContext* ctx);

int main(int argc, char* argv[]) {
    LOGI("Iniciando SA2B Static Recomp para Android...");

    // 1. Alocar os 24 MB da MEM1 do GameCube (alinhados em 32 bytes para SIMD/Paired Singles)
    if (posix_memalign((void**)&g_gc_ram, 32, GC_RAM_SIZE) != 0 || !g_gc_ram) {
        LOGE("Falha ao alocar 24MB para a RAM virtual do GameCube!");
        return -1;
    }
    memset(g_gc_ram, 0, GC_RAM_SIZE);
    memset(&g_ppc_ctx, 0, sizeof(PPCContext));

    // Configura o Stack Pointer inicial (r1) no topo da RAM (padrão Dolphin OS: 0x817FFFF0)
    g_ppc_ctx.r[1] = 0x817FFFF0;

    // 2. Inicializar SDL2 (Vídeo, Áudio e Gamepad)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        LOGE("Erro ao inicializar SDL2: %s", SDL_GetError());
        return -1;
    }

    // Configurar atributos para OpenGL ES 3.0 (necessário para emular o pipeline GX TEV)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "Sonic Adventure 2: Battle",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN
    );

    if (!window) {
        LOGE("Erro ao criar janela SDL: %s", SDL_GetError());
        return -1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1); // VSync ativo (60 FPS padrão do SA2B)

    LOGI("Contexto OpenGL ES criado: %s", glGetString(GL_VERSION));

    // 3. Passar o controle para o código recompilado do SA2B
    // gc_game_main(&g_ppc_ctx); 

    // Cleanup
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(g_gc_ram);

    return 0;
}