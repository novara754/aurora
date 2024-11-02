#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <spdlog/spdlog.h>

#include "engine.hpp"

int main()
{
    spdlog::set_level(spdlog::level::trace);

    SDL_SetAppMetadata("Aurora", "0.1", nullptr);
    spdlog::trace("main: set sdl app metadata");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        spdlog::error("main: failed to initialize sdl: {}", SDL_GetError());
        return 1;
    }
    spdlog::trace("main: initialized sdl video and audio subsystem");

    SDL_Window *window = SDL_CreateWindow("Aurora", 1280, 720, SDL_WINDOW_VULKAN);
    if (!window)
    {
        spdlog::error("main: failed to create window: {}", SDL_GetError());
        return 1;
    }
    spdlog::trace("main: created sdl window");

    try
    {
        Engine engine(window);
        if (!engine.init())
        {
            spdlog::error("main: failed to initialize engine");
        }
        else
        {
            spdlog::trace("main: initialized engine");
            spdlog::trace("main: running engine");
            engine.run();
            spdlog::trace("main: engine has exitted");
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("main: engine threw exception: {}", e.what());
    }

    SDL_DestroyWindow(window);
    spdlog::trace("main: process terminating...");
    return 0;
}
