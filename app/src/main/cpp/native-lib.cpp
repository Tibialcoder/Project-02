#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <android/log.h>

#define LOG_TAG "SA2B_Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct EngineState {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    ANativeWindow* window;
    bool initialized;
};

static EngineState engine = {EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_CONTEXT, nullptr, false};

// Função para inicializar o OpenGL ES
bool initGraphics(ANativeWindow* window) {
    if (engine.initialized) return true;

    engine.window = window;
    engine.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (engine.display == EGL_NO_DISPLAY) {
        LOGE("Falha ao obter EGLDisplay");
        return false;
    }

    eglInitialize(engine.display, 0, 0);

    const EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(engine.display, attribs, &config, 1, &numConfigs) || numConfigs == 0) {
        LOGE("Falha ao escolher configuração EGL");
        return false;
    }

    engine.surface = eglCreateWindowSurface(engine.display, config, window, NULL);
    if (engine.surface == EGL_NO_SURFACE) {
        LOGE("Falha ao criar EGLSurface");
        return false;
    }

    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    engine.context = eglCreateContext(engine.display, config, EGL_NO_CONTEXT, ctxAttribs);

    if (engine.context == EGL_NO_CONTEXT) {
        LOGE("Falha ao criar EGLContext");
        return false;
    }

    if (!eglMakeCurrent(engine.display, engine.surface, engine.surface, engine.context)) {
        LOGE("Falha ao tornar contexto atual");
        return false;
    }

    engine.initialized = true;
    LOGI("OpenGL ES 3.0 inicializado com sucesso!");
    return true;
}

// Função de loop de desenho por frame
void renderFrame() {
    if (!engine.initialized) return;

    // Limpa a tela com uma cor (ex: Azul escuro/preto para teste)
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Apresenta o buffer na tela
    eglSwapBuffers(engine.display, engine.surface);
}

// Exemplo de função chamada quando a janela nativa é criada
extern "C" JNIEXPORT void JNICALL
Java_com_google_androidgamesdk_GameActivity_nativeOnSurfaceCreated(JNIEnv* env, jobject thiz, jobject surface) {
// Nota: Em integrações reais com a GameActivity, o ponteiro da janela vem do callback do ANativeActivity/GameActivity
LOGI("Superfície criada!");
}