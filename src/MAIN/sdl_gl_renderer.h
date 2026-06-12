#pragma once

#include <cstdint>
#include <span>
#include <string>

struct SDL_Window;
using SDL_GLContext = void*;

namespace snesquik::platform {

class SdlGlRenderer {
public:
    using KeyCallback = void (*)(void* userData, int key, bool pressed);

    SdlGlRenderer() = default;
    ~SdlGlRenderer();

    SdlGlRenderer(const SdlGlRenderer&) = delete;
    SdlGlRenderer& operator=(const SdlGlRenderer&) = delete;

    bool initialize(const char* title, int sourceWidth, int sourceHeight, int scale = 3);
    bool pollEvents();
    void present(std::span<const uint32_t> rgbaPixels);
    void shutdown();
    void setKeyCallback(KeyCallback callback, void* userData);
    void setWindowTitle(const char* title);

    const std::string& lastError() const { return error; }

private:
    bool createProgram();
    void destroyGlObjects();
    void setError(const std::string& message);

    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    unsigned int program = 0;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int texture = 0;
    int width = 0;
    int height = 0;
    KeyCallback keyCallback = nullptr;
    void* keyCallbackUserData = nullptr;
    std::string error;
};

} // namespace snesquik::platform
