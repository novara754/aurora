#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <spdlog/spdlog.h>

#include "app.hpp"

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

    SDL_Window *window =
        SDL_CreateWindow("Aurora", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        spdlog::error("main: failed to create window: {}", SDL_GetError());
        return 1;
    }
    spdlog::trace("main: created sdl window");

    try
    {
        App app(window);
        if (app.init())
        {
            spdlog::trace("main: initialized app");
            spdlog::trace("main: running app");
            app.run();
            spdlog::trace("main: app has exitted");
        }
        else
        {
            spdlog::error("main: failed to initialize app");
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("main: app threw exception: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("main: app threw unknown exception");
    }

    SDL_DestroyWindow(window);
    spdlog::trace("main: process terminating...");
    return 0;
}
