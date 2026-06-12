#include "MAIN/sdl_gl_renderer.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <array>
#include <cstring>

namespace snesquik::platform {

namespace {

using GlCreateShader = GLuint (*)(GLenum);
using GlShaderSource = void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using GlCompileShader = void (*)(GLuint);
using GlGetShaderiv = void (*)(GLuint, GLenum, GLint*);
using GlGetShaderInfoLog = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlDeleteShader = void (*)(GLuint);
using GlCreateProgram = GLuint (*)();
using GlAttachShader = void (*)(GLuint, GLuint);
using GlLinkProgram = void (*)(GLuint);
using GlGetProgramiv = void (*)(GLuint, GLenum, GLint*);
using GlGetProgramInfoLog = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlDeleteProgram = void (*)(GLuint);
using GlUseProgram = void (*)(GLuint);
using GlGenVertexArrays = void (*)(GLsizei, GLuint*);
using GlBindVertexArray = void (*)(GLuint);
using GlDeleteVertexArrays = void (*)(GLsizei, const GLuint*);
using GlGenBuffers = void (*)(GLsizei, GLuint*);
using GlBindBuffer = void (*)(GLenum, GLuint);
using GlBufferData = void (*)(GLenum, GLsizeiptr, const void*, GLenum);
using GlDeleteBuffers = void (*)(GLsizei, const GLuint*);
using GlVertexAttribPointer = void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using GlEnableVertexAttribArray = void (*)(GLuint);
using GlGetUniformLocation = GLint (*)(GLuint, const GLchar*);
using GlUniform1i = void (*)(GLint, GLint);

struct Gl {
    GlCreateShader createShader = nullptr;
    GlShaderSource shaderSource = nullptr;
    GlCompileShader compileShader = nullptr;
    GlGetShaderiv getShaderiv = nullptr;
    GlGetShaderInfoLog getShaderInfoLog = nullptr;
    GlDeleteShader deleteShader = nullptr;
    GlCreateProgram createProgram = nullptr;
    GlAttachShader attachShader = nullptr;
    GlLinkProgram linkProgram = nullptr;
    GlGetProgramiv getProgramiv = nullptr;
    GlGetProgramInfoLog getProgramInfoLog = nullptr;
    GlDeleteProgram deleteProgram = nullptr;
    GlUseProgram useProgram = nullptr;
    GlGenVertexArrays genVertexArrays = nullptr;
    GlBindVertexArray bindVertexArray = nullptr;
    GlDeleteVertexArrays deleteVertexArrays = nullptr;
    GlGenBuffers genBuffers = nullptr;
    GlBindBuffer bindBuffer = nullptr;
    GlBufferData bufferData = nullptr;
    GlDeleteBuffers deleteBuffers = nullptr;
    GlVertexAttribPointer vertexAttribPointer = nullptr;
    GlEnableVertexAttribArray enableVertexAttribArray = nullptr;
    GlGetUniformLocation getUniformLocation = nullptr;
    GlUniform1i uniform1i = nullptr;
};

Gl gl;

template <typename T>
bool load(T& fn, const char* name)
{
    fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return fn != nullptr;
}

bool loadGl()
{
    return load(gl.createShader, "glCreateShader")
        && load(gl.shaderSource, "glShaderSource")
        && load(gl.compileShader, "glCompileShader")
        && load(gl.getShaderiv, "glGetShaderiv")
        && load(gl.getShaderInfoLog, "glGetShaderInfoLog")
        && load(gl.deleteShader, "glDeleteShader")
        && load(gl.createProgram, "glCreateProgram")
        && load(gl.attachShader, "glAttachShader")
        && load(gl.linkProgram, "glLinkProgram")
        && load(gl.getProgramiv, "glGetProgramiv")
        && load(gl.getProgramInfoLog, "glGetProgramInfoLog")
        && load(gl.deleteProgram, "glDeleteProgram")
        && load(gl.useProgram, "glUseProgram")
        && load(gl.genVertexArrays, "glGenVertexArrays")
        && load(gl.bindVertexArray, "glBindVertexArray")
        && load(gl.deleteVertexArrays, "glDeleteVertexArrays")
        && load(gl.genBuffers, "glGenBuffers")
        && load(gl.bindBuffer, "glBindBuffer")
        && load(gl.bufferData, "glBufferData")
        && load(gl.deleteBuffers, "glDeleteBuffers")
        && load(gl.vertexAttribPointer, "glVertexAttribPointer")
        && load(gl.enableVertexAttribArray, "glEnableVertexAttribArray")
        && load(gl.getUniformLocation, "glGetUniformLocation")
        && load(gl.uniform1i, "glUniform1i");
}

GLuint compileShader(GLenum type, const char* source, std::string& error)
{
    const GLuint shader = gl.createShader(type);
    gl.shaderSource(shader, 1, &source, nullptr);
    gl.compileShader(shader);

    GLint ok = GL_FALSE;
    gl.getShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) {
        return shader;
    }

    std::array<char, 1024> log{};
    GLsizei length = 0;
    gl.getShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &length, log.data());
    error.assign(log.data(), static_cast<size_t>(length));
    gl.deleteShader(shader);
    return 0;
}

} // namespace

SdlGlRenderer::~SdlGlRenderer()
{
    shutdown();
}

bool SdlGlRenderer::initialize(const char* title, int sourceWidth, int sourceHeight, int scale)
{
    shutdown();
    width = sourceWidth;
    height = sourceHeight;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        setError(SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width * scale, height * scale, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        setError(SDL_GetError());
        shutdown();
        return false;
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
        setError(SDL_GetError());
        shutdown();
        return false;
    }
    SDL_GL_SetSwapInterval(1);

    if (!loadGl()) {
        setError("failed to load required OpenGL entry points");
        shutdown();
        return false;
    }

    if (!createProgram()) {
        shutdown();
        return false;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    return true;
}

bool SdlGlRenderer::pollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            return false;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            return false;
        }
        if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) && keyCallback && event.key.repeat == 0) {
            keyCallback(keyCallbackUserData, event.key.keysym.sym, event.type == SDL_KEYDOWN);
        }
    }
    return true;
}

void SdlGlRenderer::present(std::span<const uint32_t> rgbaPixels)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, rgbaPixels.data());

    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    glViewport(0, 0, drawableWidth, drawableHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    gl.useProgram(program);
    gl.bindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    SDL_GL_SwapWindow(window);
}

void SdlGlRenderer::shutdown()
{
    destroyGlObjects();
    if (context) {
        SDL_GL_DeleteContext(context);
        context = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void SdlGlRenderer::setKeyCallback(KeyCallback callback, void* userData)
{
    keyCallback = callback;
    keyCallbackUserData = userData;
}

void SdlGlRenderer::setWindowTitle(const char* title)
{
    if (window) {
        SDL_SetWindowTitle(window, title);
    }
}

bool SdlGlRenderer::createProgram()
{
    static constexpr const char* vertexShader = R"glsl(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTex;
out vec2 vTex;
void main() {
    vTex = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

    static constexpr const char* fragmentShader = R"glsl(
#version 330 core
in vec2 vTex;
out vec4 fragColor;
uniform sampler2D uFrame;
void main() {
    fragColor = texture(uFrame, vTex);
}
)glsl";

    std::string shaderError;
    const GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShader, shaderError);
    if (!vs) {
        setError("vertex shader compile failed: " + shaderError);
        return false;
    }
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShader, shaderError);
    if (!fs) {
        gl.deleteShader(vs);
        setError("fragment shader compile failed: " + shaderError);
        return false;
    }

    program = gl.createProgram();
    gl.attachShader(program, vs);
    gl.attachShader(program, fs);
    gl.linkProgram(program);
    gl.deleteShader(vs);
    gl.deleteShader(fs);

    GLint ok = GL_FALSE;
    gl.getProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::array<char, 1024> log{};
        GLsizei length = 0;
        gl.getProgramInfoLog(program, static_cast<GLsizei>(log.size()), &length, log.data());
        setError("shader link failed: " + std::string(log.data(), static_cast<size_t>(length)));
        return false;
    }

    const std::array<float, 24> vertices = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
    };

    gl.genVertexArrays(1, &vao);
    gl.genBuffers(1, &vbo);
    gl.bindVertexArray(vao);
    gl.bindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
    gl.vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    gl.enableVertexAttribArray(1);

    gl.useProgram(program);
    gl.uniform1i(gl.getUniformLocation(program, "uFrame"), 0);
    return true;
}

void SdlGlRenderer::destroyGlObjects()
{
    if (texture != 0) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    if (vbo != 0 && gl.deleteBuffers) {
        gl.deleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao != 0 && gl.deleteVertexArrays) {
        gl.deleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (program != 0 && gl.deleteProgram) {
        gl.deleteProgram(program);
        program = 0;
    }
}

void SdlGlRenderer::setError(const std::string& message)
{
    error = message;
}

} // namespace snesquik::platform
