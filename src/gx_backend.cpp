#include <GLES3/gl3.h>
#include <android/log.h>
#include <vector>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SA2B_GX", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SA2B_GX", __VA_ARGS__)

// Estrutura de Vértice padrão traduzida do formato do GameCube para o GLES3
struct GXVertex {
    float position[3]; // X, Y, Z
    float normal[3];   // NX, NY, NZ
    float color[4];    // R, G, B, A
    float uv[2];       // U, V
};

// IDs dos Shaders e Programas OpenGL ES
static GLuint s_shader_program = 0;
static GLuint s_vao = 0;
static GLuint s_vbo = 0;

// Shaders básicos em GLSL para substituir o pipeline fixo do GX
const char* vertex_shader_src = R"glsl(
    #version 300 es
    layout(location = 0) in vec3 a_position;
    layout(location = 1) in vec3 a_normal;
    layout(location = 2) in vec4 a_color;
    layout(location = 3) in vec2 a_uv;

    uniform mat4 u_mvp;

    out vec4 v_color;
    out vec2 v_uv;

    void main() {
        gl_Position = u_mvp * vec4(a_position, 1.0);
        v_color = a_color;
        v_uv = a_uv;
    }
)glsl";

const char* fragment_shader_src = R"glsl(
    #version 300 es
    precision mediump float;

    in vec4 v_color;
    in vec2 v_uv;

    out vec4 fragColor;

    void main() {
        // Renderiza utilizando a cor do vértice por enquanto (futuramente mapeado via TEV)
        fragColor = v_color;
    }
)glsl";

// Função auxiliar para compilar Shaders no Android
static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOGE("Erro ao compilar shader: %s", infoLog);
    }
    return shader;
}

// Inicializa o Pipeline Gráfico Tradutor (GX -> GLES3)
bool gx_backend_init() {
    LOGI("Inicializando o GX Backend (OpenGL ES 3.0)...");

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);

    s_shader_program = glCreateProgram();
    glAttachShader(s_shader_program, vs);
    glAttachShader(s_shader_program, fs);
    glLinkProgram(s_shader_program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    // Configura VAO e VBO para os vértices do jogo
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);

    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);

    // Aloca um buffer dinâmico inicial de vértices
    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(GXVertex), nullptr, GL_DYNAMIC_DRAW);

    // Atributo de Posição (Location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, position));
    glEnableVertexAttribArray(0);

    // Atributo de Normal (Location 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, normal));
    glEnableVertexAttribArray(1);

    // Atributo de Cor (Location 2)
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, color));
    glEnableVertexAttribArray(2);

    // Atributo de Coordenada de Textura - UV (Location 3)
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GXVertex), (void*)offsetof(GXVertex, uv));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    LOGI("GX Backend inicializado com sucesso.");
    return true;
}

// Intercepta a chamada de desenho do GameCube e envia para a GPU do Android
void gx_draw_indexed_vertices(void* vertices, int vertex_count) {
    glUseProgram(s_shader_program);

    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);

    // Atualiza os dados de vértices traduzidos na GPU
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count * sizeof(GXVertex), vertices);

    // Desenha os triângulos na tela do celular
    glDrawArrays(GL_TRIANGLES, 0, vertex_count);

    glBindVertexArray(0);
}