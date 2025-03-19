#ifdef __XBOX_BUILD
#ifdef _WIN32
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT __attribute__((visibility("default")))
#endif

#include <SDL.h>
#include <glad/gl.h>
#include <imgui.h>
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include <filesystem>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include "panda_sdl/frontend_sdl.hpp"

int my_dupenv_s(char** buffer, size_t* numberOfElements, const char* varname)
{
    const char* val = getenv(varname);
    if (!val) {
        if (buffer) *buffer = nullptr;
        if (numberOfElements) *numberOfElements = 0;
        return 1; 
    }
    size_t len = strlen(val) + 1;
    if (numberOfElements) *numberOfElements = len;
    *buffer = (char*)malloc(len);
    if (!*buffer)
        return 1; 
    memcpy(*buffer, val, len);
    return 0;
}
#define _dupenv_s my_dupenv_s


namespace GameLoader {
    struct InstalledGame {
        std::string title;
        std::string id;
        std::filesystem::path path;
    };
}

std::vector<GameLoader::InstalledGame> scanGamesInDirectory(const std::filesystem::path& dir) {
    std::vector<GameLoader::InstalledGame> games;
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
        return games;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".cci" || ext == ".3ds") {
                GameLoader::InstalledGame game;
                game.title = entry.path().stem().string();
                game.id = entry.path().filename().string();
                game.path = entry.path();
                games.push_back(game);
            }
        }
    }
    return games;
}

std::vector<GameLoader::InstalledGame> scanAllGames() {
    std::vector<GameLoader::InstalledGame> allGames;
    std::filesystem::path eRoot("E:/");
    {
        auto games = scanGamesInDirectory(eRoot);
        allGames.insert(allGames.end(), games.begin(), games.end());
    }
    {
        std::filesystem::path ePanda = eRoot / "PANDA3DS";
        auto games = scanGamesInDirectory(ePanda);
        allGames.insert(allGames.end(), games.begin(), games.end());
    }
    std::filesystem::path localState;
#ifdef _WIN32
    {
        char* localStateCStr = nullptr;
        size_t len = 0;
        _dupenv_s(&localStateCStr, &len, "LOCAL_STATE_PATH");
        if (localStateCStr) {
            localState = std::filesystem::path(localStateCStr);
            free(localStateCStr);
        }
    }
#else
    {
        const char* localStateCStr = getenv("LOCAL_STATE_PATH");
        if (localStateCStr) {
            localState = std::filesystem::path(localStateCStr);
        }
    }
#endif
    if (!localState.empty()) {
        auto games = scanGamesInDirectory(localState);
        allGames.insert(allGames.end(), games.begin(), games.end());
        std::filesystem::path localStatePanda = localState / "PANDA3DS";
        auto games2 = scanGamesInDirectory(localStatePanda);
        allGames.insert(allGames.end(), games2.begin(), games2.end());
    }
    return allGames;
}

static int ImGuiGameSelector(const std::vector<GameLoader::InstalledGame>& games) {
    SDL_Window* currentWindow  = SDL_GL_GetCurrentWindow();
    SDL_GLContext currentContext = SDL_GL_GetCurrentContext();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL; 
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; 
    ImGui_ImplSDL2_InitForOpenGL(currentWindow, currentContext);
    ImGui_ImplOpenGL3_Init("#version 410");

    int selected = 0;
    bool selectionMade = false;
    while (!selectionMade) {
        int drawableWidth, drawableHeight;
        SDL_GL_GetDrawableSize(currentWindow, &drawableWidth, &drawableHeight);
        glViewport(0, 0, drawableWidth, drawableHeight);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                exit(0);
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP && selected > 0)
                    selected--;
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN &&
                         selected < static_cast<int>(games.size()) - 1)
                    selected++;
                else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
                    selectionMade = true;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_UP && selected > 0)
                    selected--;
                else if (event.key.keysym.sym == SDLK_DOWN &&
                         selected < static_cast<int>(games.size()) - 1)
                    selected++;
                else if (event.key.keysym.sym == SDLK_RETURN)
                    selectionMade = true;
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(currentWindow);
        ImGui::NewFrame();
        int width, height;
        SDL_GetWindowSize(currentWindow, &width, &height);
        ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f)); 
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
        ImGui::Begin("Select Game", nullptr, flags);
        for (int i = 0; i < static_cast<int>(games.size()); i++) {
            std::string label = std::to_string(i) + ": " + games[i].title + " (ID: " + games[i].id + " PATH: " + games[i].path.string() + ")";   
            if (ImGui::Selectable(label.c_str(), selected == i))
                selected = i;
        }
        ImGui::End();
        ImGui::Render();
        SDL_GL_MakeCurrent(currentWindow, currentContext);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(currentWindow);
        SDL_Delay(16);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    return selected;
}

static void ShowAlertWithOK(const std::string& message) {
    SDL_Window* currentWindow  = SDL_GL_GetCurrentWindow();
    SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad; 
    io.IniFilename = NULL;
    ImGui_ImplSDL2_InitForOpenGL(currentWindow, currentContext);
    ImGui_ImplOpenGL3_Init("#version 410");
    bool done = false;
    while (!done) {
        int drawableWidth, drawableHeight;
        SDL_GL_GetDrawableSize(currentWindow, &drawableWidth, &drawableHeight);
        glViewport(0, 0, drawableWidth, drawableHeight);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                exit(0);
            if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN) ||
                (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A))
            {
                done = true;
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(currentWindow);
        ImGui::NewFrame();
        int w, h;
        SDL_GetWindowSize(currentWindow, &w, &h);
        ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Always);
        ImGui::Begin("Alert", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::Text("Press Enter or A to retry.");
        ImGui::End();
        ImGui::Render();
        SDL_GL_MakeCurrent(currentWindow, currentContext);
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(currentWindow);
        SDL_Delay(16);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

// Displays a critical error alert and then freezes the application.
static void ShowCriticalAlertAndFreeze(const std::string& message) {
    SDL_Window* currentWindow  = SDL_GL_GetCurrentWindow();
    SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad; 
    io.IniFilename = NULL;
    ImGui_ImplSDL2_InitForOpenGL(currentWindow, currentContext);
    ImGui_ImplOpenGL3_Init("#version 410");
    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                exit(0);
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(currentWindow);
        ImGui::NewFrame();
        int w, h;
        SDL_GetWindowSize(currentWindow, &w, &h);
        ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Always);
        ImGui::Begin("Critical Error", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::Text("Application will now freeze.");
        ImGui::End();
        ImGui::Render();
        SDL_GL_MakeCurrent(currentWindow, currentContext);
        int drawableWidth, drawableHeight;
        SDL_GL_GetDrawableSize(currentWindow, &drawableWidth, &drawableHeight);
        glViewport(0, 0, drawableWidth, drawableHeight);
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(currentWindow);
        SDL_Delay(16);
    }
    // (Never reached)
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

extern "C" EXPORT int external_main(SDL_Window* host_window, SDL_GLContext host_context, int argc, char** argv) {
    try {
        printf("Alber external_main: Started\n");
        
        if (SDL_GL_MakeCurrent(host_window, host_context) != 0) {
            printf("external_main: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
            return -1;
        }
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            throw std::runtime_error("Failed to initialize GLAD");
        }
        printf("external_main: GL context is now current\n");

        std::filesystem::path file = "";
        std::filesystem::path localStatePath = "";
        if (argc >= 2) {
            file = argv[1];
            printf("external_main: File provided: %s\n", file.generic_string().c_str());
        } else {
            printf("external_main: No file provided, will use boot window/game selector\n");
        }
        if (argc >= 3) {
            localStatePath = argv[2];
            printf("external_main: Local state path provided: %s\n", localStatePath.generic_string().c_str());
            std::string envVarName = "LOCAL_STATE_PATH";
            std::string envVarValue = localStatePath.generic_string();
        #ifdef _WIN32
            if (_putenv_s(envVarName.c_str(), envVarValue.c_str()) != 0)
                printf("external_main: Failed to set environment variable %s\n", envVarName.c_str());
            else
                printf("external_main: Environment variable %s set to %s\n", envVarName.c_str(), envVarValue.c_str());
        #else
            if (setenv(envVarName.c_str(), envVarValue.c_str(), 1) != 0)
                printf("external_main: Failed to set environment variable %s\n", envVarName.c_str());
            else
                printf("external_main: Environment variable %s set to %s\n", envVarName.c_str(), envVarValue.c_str());
        #endif
        } else {
            printf("external_main: Local state path not provided as argument.\n");
        }
        
        std::string result = ""; 
        if (!result.empty()) {
            file = result;
        }
        
        std::vector<GameLoader::InstalledGame> allGames = scanAllGames();
        while (allGames.empty()) {
            ShowAlertWithOK("No ROM inserted!\n\nPlease add ROMs (.cci or .3ds files) to one of the following folders:\nE:/\nE:/PANDA3DS\n[LOCAL_STATE_PATH]\n[LOCAL_STATE_PATH]/PANDA3DS");
            allGames = scanAllGames();
        }
        
        int selectedIndex = ImGuiGameSelector(allGames);
        std::filesystem::path selectedGamePath = allGames[selectedIndex].path;
        
        FrontendSDL app;
        if (!app.loadROM(selectedGamePath)) {
            printf("Failed to load ROM file: %s\n", selectedGamePath.string().c_str());
            return -1;
        }
        app.run();
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "Exception caught in external_main: %s\n", e.what());
        ShowCriticalAlertAndFreeze(std::string("Exception: ") + e.what());
        return -1;
    } catch (...) {
        fprintf(stderr, "Unknown exception caught in external_main.\n");
        ShowCriticalAlertAndFreeze("Unknown exception caught.\n");
        return -1;
    }
}

#else

#include "panda_sdl/frontend_sdl.hpp"
#include <filesystem>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    FrontendSDL app;
    if (argc > 1) {
        auto romPath = std::filesystem::current_path() / argv[1];
        if (!app.loadROM(romPath))
            printf("Failed to load ROM file: %s\n", romPath.string().c_str());
    } else {
        printf("No ROM inserted! Load a ROM by dragging and dropping it into the emulator window!\n");
    }
    app.run();
    return 0;
}

#endif