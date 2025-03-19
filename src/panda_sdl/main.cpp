#define __XBOX_BUILD 1
#ifdef __XBOX_BUILD
    #ifdef _WIN32
        #define EXPORT __declspec(dllexport)
    #else
        #define EXPORT __attribute__((visibility("default")))
    #endif

    #include "panda_sdl/frontend_sdl.hpp"
    #include <glad/gl.h>
    #include <SDL.h>
    #include <filesystem>

    extern "C" EXPORT int external_main(SDL_Window* host_window, SDL_GLContext host_context, int argc, char* argv[]) {
        // Force the GL context to be current on this thread.
        if (SDL_GL_MakeCurrent(host_window, host_context) != 0) {
            Helpers::panic("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        }
        
        // Initialize GLAD to load OpenGL functions.
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            Helpers::panic("Failed to initialize GLAD");
        }

        FrontendSDL app;

        if (argc > 1) {
            auto romPath = std::filesystem::current_path() / argv[1];
            if (!app.loadROM(romPath)) {
                // For some reason just .c_str() doesn't show the proper path
                Helpers::panic("Failed to load ROM file: %s", romPath.string().c_str());
            }
        } else {
            printf("No ROM inserted! Load a ROM by dragging and dropping it into the emulator window!\n");
        }

        app.run();
        return 0;
    }
#else
    #include "panda_sdl/frontend_sdl.hpp"
    #include <filesystem>
    #include <cstdio>

    int main(int argc, char *argv[]) {
        FrontendSDL app;

        if (argc > 1) {
            auto romPath = std::filesystem::current_path() / argv[1];
            if (!app.loadROM(romPath)) {
                // For some reason just .c_str() doesn't show the proper path
                Helpers::panic("Failed to load ROM file: %s", romPath.string().c_str());
            }
        } else {
            printf("No ROM inserted! Load a ROM by dragging and dropping it into the emulator window!\n");
        }

        app.run();
        return 0;
    }
#endif