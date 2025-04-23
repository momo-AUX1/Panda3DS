#ifdef __XBOX_BUILD
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#include <SDL.h>
#include <glad/gl.h>
#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.hpp"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"
#include "panda_sdl/frontend_sdl.hpp"

static bool s_is_durango = false;

int my_dupenv_s(char** buffer, size_t* numberOfElements, const char* varname) {
	const char* val = getenv(varname);
	if (!val) {
		if (buffer) *buffer = nullptr;
		if (numberOfElements) *numberOfElements = 0;
		return 1;
	}
	size_t len = strlen(val) + 1;
	if (numberOfElements) *numberOfElements = len;
	*buffer = (char*)malloc(len);
	if (!*buffer) return 1;
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
}  // namespace GameLoader

std::vector<GameLoader::InstalledGame> scanGamesInDirectory(const std::filesystem::path& dir) {
	std::vector<GameLoader::InstalledGame> games;
	if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) return games;
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		if (entry.is_regular_file()) {
			auto ext = entry.path().extension().string();
			if (ext == ".cci" || ext == ".3ds" || ext == ".cxi" || ext == ".app" || ext == ".ncch" || ext == ".elf" || ext == ".axf" ||
				ext == ".3dsx") {
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
	SDL_Window* currentWindow = SDL_GL_GetCurrentWindow();
	SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
	static EmulatorConfig cfg("config.toml");
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	ImGui_ImplSDL2_InitForOpenGL(currentWindow, currentContext);
#ifdef __XBOX_ONE_BUILD
	ImGui_ImplOpenGL3_Init("#version 300 es");  
#else
	ImGui_ImplOpenGL3_Init("#version 410");  
#endif

	bool inSettings = false;
	int selected = 0;
	bool selectionMade = false;

	while (!selectionMade) {
		int drawableW, drawableH;
		SDL_GL_GetDrawableSize(currentWindow, &drawableW, &drawableH);
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) exit(0);
			if (!inSettings && event.type == SDL_CONTROLLERBUTTONDOWN) {
				if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP && selected > 0) selected--;
				if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN && selected < (int)games.size() - 1) selected++;
				if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) selectionMade = true;
			}
			if (!inSettings && event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == SDLK_UP && selected > 0) selected--;
				if (event.key.keysym.sym == SDLK_DOWN && selected < (int)games.size() - 1) selected++;
				if (event.key.keysym.sym == SDLK_RETURN) selectionMade = true;
			}
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(currentWindow);
		ImGui::NewFrame();
		#ifdef __XBOX_ONE_BUILD
		int drawableWX, drawableHX;
		SDL_GL_GetDrawableSize(currentWindow, &drawableWX, &drawableHX);
		ImGui::GetIO().DisplaySize = ImVec2((float)drawableWX, (float)drawableHX);
		#else

		#endif

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

		if (!inSettings) {
			// ─── Game selector ───────────────────────────────────────────────────────
			ImGui::SetNextWindowPos(ImVec2(drawableW * 0.5f, drawableH * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_Always);
			ImGui::Begin("Select Game", nullptr, flags);

			for (int i = 0; i < (int)games.size(); i++) {
				char buf[512];
				snprintf(
					buf, sizeof(buf), "%d: %s (ID: %s PATH: %s)", i, games[i].title.c_str(), games[i].id.c_str(), games[i].path.string().c_str()
				);
				if (ImGui::Selectable(buf, selected == i)) selected = i;
			}
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::SetCursorPosX((800 - 120) * 0.5f);
            if (ImGui::Button("Settings", ImVec2(120, 0))) {
				inSettings = true;
		
				SDL_Event clearEvent;
				while (SDL_PollEvent(&clearEvent)) {
				}
			}

            ImGui::End();
		} else {
			// ─── Settings page ────────────────────────────────────────────────────────
			ImGui::SetNextWindowPos(ImVec2(drawableW * 0.5f, drawableH * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_Always);
			ImGui::Begin("Settings", nullptr, flags);

			// --- General ---
			if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("Enable Discord RPC", &cfg.discordRpcEnabled);
				ImGui::Checkbox("Use Portable Build", &cfg.usePortableBuild);
				ImGui::Checkbox("Print App Version", &cfg.printAppVersion);
				static char romPath[256];
				snprintf(romPath, sizeof(romPath), "%s", cfg.defaultRomPath.string().c_str());
				if (ImGui::InputText("Default ROM Path", romPath, sizeof(romPath))) {
					cfg.defaultRomPath = romPath;
				}
				const char* langs[] = {"ja", "en", "fr", "de", "it", "es", "zh", "ko", "nl", "pt", "ru", "tw"};
				int sysLang = (int)cfg.systemLanguage;
				if (ImGui::Combo("System Language", &sysLang, langs, IM_ARRAYSIZE(langs))) {
					cfg.systemLanguage = EmulatorConfig::languageCodeFromString(langs[sysLang]);
				}
			}

			// --- Window ---
			if (ImGui::CollapsingHeader("Window")) {
				ImGui::Checkbox("Show App Version", &cfg.windowSettings.showAppVersion);
				ImGui::Checkbox("Remember Position", &cfg.windowSettings.rememberPosition);
				ImGui::InputInt("Pos X", &cfg.windowSettings.x);
				ImGui::InputInt("Pos Y", &cfg.windowSettings.y);
				ImGui::InputInt("Width", &cfg.windowSettings.width);
				ImGui::InputInt("Height", &cfg.windowSettings.height);
			}

			// --- GPU ---
			if (ImGui::CollapsingHeader("GPU")) {
                const char* renderers[] = {"", "OpenGL", "Vulkan", "Metal"};
                int rend = (int)cfg.rendererType;
                if (rend < 0 || rend >= (IM_ARRAYSIZE(renderers))) rend = 1; 
                if (ImGui::Combo("Renderer", &rend, renderers, IM_ARRAYSIZE(renderers))) {
                    if (rend == 0) {
                        cfg.rendererType = Renderer::typeFromString("OpenGL").value(); 
                    } else {
                        cfg.rendererType = Renderer::typeFromString(renderers[rend]).value();
                    }
                }
				ImGui::Checkbox("Enable VSync", &cfg.vsyncEnabled);
				ImGui::Checkbox("Enable RenderDoc", &cfg.enableRenderdoc);
				ImGui::Checkbox("Enable Shader JIT", &cfg.shaderJitEnabled);
				ImGui::Checkbox("Use Ubershaders", &cfg.useUbershaders);
				ImGui::Checkbox("Accelerate Shaders", &cfg.accelerateShaders);
				ImGui::Checkbox("Accurate Shader Mul", &cfg.accurateShaderMul);
				ImGui::Checkbox("Force Shadergen Lights", &cfg.forceShadergenForLights);
				ImGui::InputInt("Light Shader Threshold", &cfg.lightShadergenThreshold);
			}

			// --- Audio ---
			if (ImGui::CollapsingHeader("Audio")) {
				const char* dspCores[] = {"HLE", "LLE"};
				int dsp = (int)cfg.dspType;
				if (dsp < 0 || dsp >= IM_ARRAYSIZE(dspCores)) dsp = 0;
				if (ImGui::Combo("DSP Emulation", &dsp, dspCores, IM_ARRAYSIZE(dspCores)))
					cfg.dspType = Audio::DSPCore::typeFromString(dspCores[dsp]);
				ImGui::Checkbox("Enable Audio", &cfg.audioEnabled);
				ImGui::Checkbox("Enable AAC", &cfg.aacEnabled);
				ImGui::Checkbox("Mute Audio", &cfg.audioDeviceConfig.muteAudio);
				ImGui::SliderFloat("Volume", &cfg.audioDeviceConfig.volumeRaw, 0.0f, 2.0f);
				const char* curves[] = {"cubic", "linear"};
				int curve = (int)cfg.audioDeviceConfig.volumeCurve;
				if (ImGui::Combo("Volume Curve", &curve, curves, IM_ARRAYSIZE(curves)))
					cfg.audioDeviceConfig.volumeCurve = AudioDeviceConfig::volumeCurveFromString(curves[curve]);
				ImGui::Checkbox("Print DSP Firmware", &cfg.printDSPFirmware);
			}

			// --- Battery ---
			if (ImGui::CollapsingHeader("Battery")) {
				ImGui::Checkbox("Charger Plugged", &cfg.chargerPlugged);
				ImGui::InputInt("Battery Percentage", &cfg.batteryPercentage);
			}

			// --- SD ---
			if (ImGui::CollapsingHeader("SD")) {
				ImGui::Checkbox("Use Virtual SD", &cfg.sdCardInserted);
				ImGui::Checkbox("Write Protect SD", &cfg.sdWriteProtected);
			}

			// --- UI ---
			if (ImGui::CollapsingHeader("UI")) {
				const char* themes[] = {"dark", "light"};
				int theme = (int)cfg.frontendSettings.theme;
				if (ImGui::Combo("Theme", &theme, themes, IM_ARRAYSIZE(themes)))
					cfg.frontendSettings.theme = FrontendSettings::themeFromString(themes[theme]);
				const char* icons[] = {"rpog", "custom"};
				int icon = (int)cfg.frontendSettings.icon;
				if (ImGui::Combo("Icon", &icon, icons, IM_ARRAYSIZE(icons)))
					cfg.frontendSettings.icon = FrontendSettings::iconFromString(icons[icon]);
				static char uiLang[8];
				strncpy(uiLang, cfg.frontendSettings.language.c_str(), sizeof(uiLang));
				if (ImGui::InputText("Language", uiLang, sizeof(uiLang))) cfg.frontendSettings.language = uiLang;
			}

			// --- Xbox Specific ---
#ifdef __XBOX_BUILD
			if (ImGui::CollapsingHeader("Xbox Specific")) {
				ImGui::Checkbox("Show Debug Info", &cfg.xboxSpecific.debugInfo);
                ImGui::Checkbox("Stretch Window (ignores resolution)",     &cfg.xboxSpecific.stretchWindow);
#ifdef __XBOX_ONE_BUILD
				if (s_is_durango) {
					const char* glBackends[] = {"OpenGLES"};
					int gb = (int)cfg.xboxSpecific.glBackend;
					if (gb != (int)EmulatorConfig::XboxSettings::GLBackend::OpenGLES) {
						gb = 0; 
					}
					if (ImGui::Combo("GL Backend", &gb, glBackends, IM_ARRAYSIZE(glBackends))) {
						cfg.xboxSpecific.glBackend = EmulatorConfig::XboxSettings::GLBackend::OpenGLES;
						SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
							"Renderer Change",
							"Renderer change will take effect after next app reboot.",
							currentWindow);
					}
				} else {
					const char* glBackends[] = {"DesktopGL", "OpenGLES"};
					int gb = (int)cfg.xboxSpecific.glBackend;
					if (gb < 0 || gb >= IM_ARRAYSIZE(glBackends)) {
						gb = 0;  
					}
					if (ImGui::Combo("GL Backend", &gb, glBackends, IM_ARRAYSIZE(glBackends))) {
						cfg.xboxSpecific.glBackend = (EmulatorConfig::XboxSettings::GLBackend)gb;
						SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
							"Renderer Change",
							"Renderer change will take effect after next app reboot.",
							currentWindow);
					}
				}
#else
				const char* glBackends[] = {"DesktopGL", "OpenGLES"};
				int gb = (int)cfg.xboxSpecific.glBackend;
				if (ImGui::Combo("GL Backend", &gb, glBackends, IM_ARRAYSIZE(glBackends))) {
						cfg.xboxSpecific.glBackend = (EmulatorConfig::XboxSettings::GLBackend)gb;
						SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
							"Renderer Change",
							"Renderer change will take effect after next app reboot.",
							currentWindow);
				}
#endif
				const char* audioBackends[] = {"SDL", "WASAPI", "OSS"};
				int ab = (int)cfg.xboxSpecific.audioBackend;
				if (ImGui::Combo("Audio Backend", &ab, audioBackends, IM_ARRAYSIZE(audioBackends)))
					cfg.xboxSpecific.audioBackend = (EmulatorConfig::XboxSettings::AudioBackend)ab;
			}
#endif

#if defined(__DEVSTORE_BUILD)
            // --- DevStore ---
            if (ImGui::CollapsingHeader("DevStore")) {
                static char devStoreKeyBuf[256];
                snprintf(devStoreKeyBuf, sizeof(devStoreKeyBuf), "%s", cfg.devStoreSettings.secretKey.c_str());
                if (ImGui::InputText("Secret Key", devStoreKeyBuf, sizeof(devStoreKeyBuf))) {
                    cfg.devStoreSettings.secretKey = devStoreKeyBuf;
                }
                ImGui::Checkbox("Enable Cloud Saves", &cfg.devStoreSettings.enableCloudSaves);
            }
#endif

			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, 8));
			ImGui::SetCursorPosX((500 - 80) * 0.5f);
			if (ImGui::Button("Back", ImVec2(80, 0))) {
				cfg.save();
				inSettings = false;
			}

			ImGui::End();
		}

		ImGui::Render();
		SDL_GL_MakeCurrent(currentWindow, currentContext);
		glViewport(0, 0, drawableW, drawableH);
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
	SDL_Window* currentWindow = SDL_GL_GetCurrentWindow();
	SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = NULL;
	ImGui_ImplSDL2_InitForOpenGL(currentWindow, currentContext);
#ifdef __XBOX_ONE_BUILD
	ImGui_ImplOpenGL3_Init("#version 300 es");  
#else
	ImGui_ImplOpenGL3_Init("#version 410");  
#endif
	bool done = false;
	while (!done) {
		int drawableWidth, drawableHeight;
		SDL_GL_GetDrawableSize(currentWindow, &drawableWidth, &drawableHeight);
		glViewport(0, 0, drawableWidth, drawableHeight);
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) exit(0);
			if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN) ||
				(event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A)) {
				done = true;
			}
		}
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(currentWindow);
		ImGui::NewFrame();
		#ifdef __XBOX_ONE_BUILD
		int drawableWX, drawableHX;
		SDL_GL_GetDrawableSize(currentWindow, &drawableWX, &drawableHX);
		ImGui::GetIO().DisplaySize = ImVec2((float)drawableWX, (float)drawableHX);
		#else

		#endif
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

static void ShowCriticalAlertAndFreeze(const std::string& message) {
	SDL_Window* currentWindow = SDL_GL_GetCurrentWindow();
	SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = NULL;
	ImGui_ImplSDL2_InitForOpenGL(currentWindow, currentContext);
#ifdef __XBOX_ONE_BUILD
	ImGui_ImplOpenGL3_Init("#version 300 es");  
#else
	ImGui_ImplOpenGL3_Init("#version 410");  
#endif
	while (true) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) exit(0);
		}
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(currentWindow);
		ImGui::NewFrame();
		#ifdef __XBOX_ONE_BUILD
		int drawableWX, drawableHX;
		SDL_GL_GetDrawableSize(currentWindow, &drawableWX, &drawableHX);
		ImGui::GetIO().DisplaySize = ImVec2((float)drawableWX, (float)drawableHX);
		#else

		#endif
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
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
}

extern "C" EXPORT int external_main(SDL_Window* host_window, SDL_GLContext host_context, bool is_durango, int argc, char** argv) {
	try {
		printf("Alber external_main: Started\n");
		s_is_durango = is_durango;

		if (SDL_GL_MakeCurrent(host_window, host_context) != 0) {
			printf("external_main: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
			return -1;
		}
		#ifdef __XBOX_ONE_BUILD
		if (!gladLoadGLES2Loader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
			Helpers::panic("OpenGL ES init failed");
		}
		#else
		if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
			throw std::runtime_error("Failed to initialize GLAD");
		}
		#endif
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
			ShowAlertWithOK(
				"No ROM inserted!\n\nPlease add ROMs (supported formats: .cci, .3ds, .cxi, .app, .ncch, .elf, .axf, .3dsx) to one of the following "
				"folders:\nE:/\nE:/PANDA3DS\n[LOCAL_STATE_PATH]\n[LOCAL_STATE_PATH]/PANDA3DS"
			);
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

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "panda_sdl/frontend_sdl.hpp"

int main(int argc, char** argv) {
	FrontendSDL app;
	if (argc > 1) {
		auto romPath = std::filesystem::current_path() / argv[1];
		if (!app.loadROM(romPath)) printf("Failed to load ROM file: %s\n", romPath.string().c_str());
	} else {
		printf("No ROM inserted! Load a ROM by dragging and dropping it into the emulator window!\n");
	}
	app.run();
	return 0;
}

#endif