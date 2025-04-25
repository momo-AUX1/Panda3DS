#include "panda_sdl/frontend_sdl.hpp"

#include <glad/gl.h>

#include "renderdoc.hpp"
#include "sdl_sensors.hpp"
#include "version.hpp"

#ifdef __XBOX_BUILD
#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#endif

FrontendSDL::FrontendSDL() : keyboardMappings(InputMappings::defaultKeyboardMappings()) {
#ifndef __XBOX_BUILD
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
		Helpers::panic("Failed to initialize SDL2");
	}

	// Make SDL use consistent positional button mapping
	SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
	if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) {
		Helpers::warn("Failed to initialize SDL2 GameController: %s", SDL_GetError());
	}
#endif

	if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
		gameController = SDL_GameControllerOpen(0);

		if (gameController != nullptr) {
			SDL_Joystick* stick = SDL_GameControllerGetJoystick(gameController);
			gameControllerID = SDL_JoystickInstanceID(stick);
		}

		setupControllerSensors(gameController);
	}

	const EmulatorConfig& config = emu.getConfig();
#ifdef __XBOX_BUILD
	const bool debugInfo = config.xboxSpecific.debugInfo;
	const bool stretchWindow = config.xboxSpecific.stretchWindow;
#endif
	// We need OpenGL for software rendering/null renderer or for the OpenGL renderer if it's enabled.
	bool needOpenGL = config.rendererType == RendererType::Software || config.rendererType == RendererType::Null;
#ifdef PANDA3DS_ENABLE_OPENGL
	needOpenGL = needOpenGL || (config.rendererType == RendererType::OpenGL);
#endif

	const char* windowTitle = config.windowSettings.showAppVersion ? ("Alber v" PANDA3DS_VERSION) : "Alber";
	if (config.printAppVersion) {
		printf("Welcome to Panda3DS v%s!\n", PANDA3DS_VERSION);
	}

	// Positions of the window
	int windowX, windowY;

#ifdef __XBOX_BUILD
	if (stretchWindow) {
		int drawableWidthX, drawableHeightX;
		SDL_Window* currentWindowX = SDL_GL_GetCurrentWindow();
		SDL_GL_GetDrawableSize(currentWindowX, &drawableWidthX, &drawableHeightX);
		windowX = SDL_WINDOWPOS_CENTERED;
		windowY = SDL_WINDOWPOS_CENTERED;
		emu.setOutputSize(drawableWidthX, drawableHeightX);
		glViewport(0, 0, drawableWidthX, drawableHeightX);
	} else {
		if (config.windowSettings.rememberPosition) {
			windowX = config.windowSettings.x;
			windowY = config.windowSettings.y;
			windowWidth = config.windowSettings.width;
			windowHeight = config.windowSettings.height;
		} else {
			windowX = SDL_WINDOWPOS_CENTERED;
			windowY = SDL_WINDOWPOS_CENTERED;
			windowWidth = 400;
			windowHeight = 480;
		}
	}
#else
	if (config.windowSettings.rememberPosition) {
		windowX = config.windowSettings.x;
		windowY = config.windowSettings.y;
		windowWidth = config.windowSettings.width;
		windowHeight = config.windowSettings.height;
	} else {
		windowX = SDL_WINDOWPOS_CENTERED;
		windowY = SDL_WINDOWPOS_CENTERED;
		windowWidth = 400;
		windowHeight = 480;
	}
	emu.setOutputSize(windowWidth, windowHeight);
#endif

#ifdef __XBOX_BUILD
	if (needOpenGL) {
		window = SDL_GL_GetCurrentWindow();
		if (window == nullptr) {
			Helpers::panic("No current SDL window found");
		}
		glContext = SDL_GL_GetCurrentContext();
		if (glContext == nullptr) {
			Helpers::panic("No current OpenGL context found");
		}
		#ifdef __XBOX_ONE_BUILD
		if (!gladLoadGLES2Loader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
			Helpers::panic("OpenGL ES init failed");
		}
		emu.getRenderer()->setupGLES();
		#else
		if (config.rendererType == RendererType::OpenGL) {
			if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
				Helpers::panic("OpenGL init failed");
			}
		} else {
			if (!gladLoadGLES2Loader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
				Helpers::panic("OpenGL ES init failed");
			}
			emu.getRenderer()->setupGLES();
		}
		#endif
		SDL_GL_SetSwapInterval(config.vsyncEnabled ? 1 : 0);
	}
#else
	if (needOpenGL) {
		// Demand 4.1 core for OpenGL renderer (max available on MacOS), 3.3 for the software & null renderers
		// MacOS gets mad if we don't explicitly demand a core profile
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, config.rendererType == RendererType::OpenGL ? 4 : 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, config.rendererType == RendererType::OpenGL ? 1 : 3);
		window = SDL_CreateWindow(windowTitle, windowX, windowY, windowWidth, windowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

		if (window == nullptr) {
			Helpers::panic("Window creation failed: %s", SDL_GetError());
		}

		glContext = SDL_GL_CreateContext(window);
		if (glContext == nullptr) {
			Helpers::warn("OpenGL context creation failed: %s\nTrying again with OpenGL ES.", SDL_GetError());

			// Some low end devices (eg RPi, emulation handhelds) don't support desktop GL, but only OpenGL ES, so fall back to that if GL context
			// creation failed
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
			glContext = SDL_GL_CreateContext(window);
			if (glContext == nullptr) {
				Helpers::panic("OpenGL context creation failed: %s", SDL_GetError());
			}

			if (!gladLoadGLES2Loader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
				Helpers::panic("OpenGL init failed");
			}

			emu.getRenderer()->setupGLES();
		} else {
			if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
				Helpers::panic("OpenGL init failed");
			}
		}

		SDL_GL_SetSwapInterval(config.vsyncEnabled ? 1 : 0);
	}
#endif

#ifdef PANDA3DS_ENABLE_VULKAN
	if (config.rendererType == RendererType::Vulkan) {
		window = SDL_CreateWindow(windowTitle, windowX, windowY, windowWidth, windowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

		if (window == nullptr) {
			Helpers::warn("Window creation failed: %s", SDL_GetError());
		}
	}
#endif

#ifdef PANDA3DS_ENABLE_METAL
	if (config.rendererType == RendererType::Metal) {
		window = SDL_CreateWindow(windowTitle, windowX, windowY, windowWidth, windowHeight, SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE);

		if (window == nullptr) {
			Helpers::warn("Window creation failed: %s", SDL_GetError());
		}
	}
#endif

#ifdef __XBOX_BUILD
        ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
	   	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    #ifdef __XBOX_ONE_BUILD
        ImGui_ImplOpenGL3_Init("#version 300 es");
    #else
        ImGui_ImplOpenGL3_Init("#version 410");
    #endif
#endif

	emu.initGraphicsContext(window);
}

bool FrontendSDL::loadROM(const std::filesystem::path& path) { return emu.loadROM(path); }

void FrontendSDL::run() {
	programRunning = true;
	keyboardAnalogX = false;
	keyboardAnalogY = false;
	holdingRightClick = false;
	#ifdef __XBOX_BUILD
    const bool debugInfo = emu.getConfig().xboxSpecific.debugInfo;
	bool overlayOpen = false;
    bool startHeld = false;
    bool selectHeld = false;
	#endif

	while (programRunning) {
		SDL_GL_MakeCurrent(window, glContext);

		// Query the full window size
		int fullW, fullH;
		SDL_GL_GetDrawableSize(window, &fullW, &fullH);

		glViewport(0, 0, fullW, fullH);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

#ifdef __XBOX_BUILD
		const auto& cfg = emu.getConfig();
		if (cfg.xboxSpecific.stretchWindow) {
			emu.setOutputSize(fullW, fullH);
			glViewport(0, 0, fullW, fullH);
		} else {
			constexpr int baseW = 400, baseH = 480;
			float scale = std::min(fullW / float(baseW), fullH / float(baseH));
			int outW = int(baseW * scale), outH = int(baseH * scale);
			int offX = (fullW - outW) / 2, offY = (fullH - outH) / 2;
			emu.setOutputSize(outW, outH);
			glViewport(offX, offY, outW, outH);
		}
#endif
#ifdef PANDA3DS_ENABLE_HTTP_SERVER
		httpServer.processActions();
#endif

		emu.runFrame();
		HIDService& hid = emu.getServiceManager().getHID();
		SDL_Window* currentWindowXX = SDL_GL_GetCurrentWindow();

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
#ifdef __XBOX_BUILD
			ImGui_ImplSDL2_ProcessEvent(&event);
			int drawableWidthXX, drawableHeightXX;
			SDL_GL_GetDrawableSize(currentWindowXX, &drawableWidthXX, &drawableHeightXX);
			glViewport(0, 0, drawableWidthXX, drawableHeightXX);
#endif

			namespace Keys = HID::Keys;

			switch (event.type) {
				case SDL_QUIT: {
					printf("Bye :(\n");
					programRunning = false;
					// Remember window position & size for future runs
					auto& windowSettings = emu.getConfig().windowSettings;
					SDL_GetWindowPosition(window, &windowSettings.x, &windowSettings.y);
					SDL_GetWindowSize(window, &windowSettings.width, &windowSettings.height);
					return;
				}

				case SDL_KEYDOWN: {
					if (emu.romType == ROMType::None) break;

					u32 key = getMapping(event.key.keysym.sym);
					if (key != HID::Keys::Null) {
						switch (key) {
							case HID::Keys::CirclePadRight:
								hid.setCirclepadX(0x9C);
								keyboardAnalogX = true;
								break;
							case HID::Keys::CirclePadLeft:
								hid.setCirclepadX(-0x9C);
								keyboardAnalogX = true;
								break;
							case HID::Keys::CirclePadUp:
								hid.setCirclepadY(0x9C);
								keyboardAnalogY = true;
								break;
							case HID::Keys::CirclePadDown:
								hid.setCirclepadY(-0x9C);
								keyboardAnalogY = true;
								break;
							default: hid.pressKey(key); break;
						}
					} else {
						switch (event.key.keysym.sym) {
							// Use the F4 button as a hot-key to pause or resume the emulator
							// We can't use the audio play/pause buttons because it's annoying
							case SDLK_F4: {
								emu.togglePause();
								break;
							}

							// Use F5 as a reset button
							case SDLK_F5: {
								emu.reset(Emulator::ReloadOption::Reload);
								break;
							}

							case SDLK_F11: {
								if constexpr (Renderdoc::isSupported()) {
									Renderdoc::triggerCapture();
								}

								break;
							}
						}
					}
					break;
				}

				case SDL_KEYUP: {
					if (emu.romType == ROMType::None) break;

					u32 key = getMapping(event.key.keysym.sym);
					if (key != HID::Keys::Null) {
						switch (key) {
							// Err this is probably not ideal
							case HID::Keys::CirclePadRight:
							case HID::Keys::CirclePadLeft:
								hid.setCirclepadX(0);
								keyboardAnalogX = false;
								break;
							case HID::Keys::CirclePadUp:
							case HID::Keys::CirclePadDown:
								hid.setCirclepadY(0);
								keyboardAnalogY = false;
								break;
							default: hid.releaseKey(key); break;
						}
					}
					break;
				}

				case SDL_MOUSEBUTTONDOWN:
					if (emu.romType == ROMType::None) break;

					if (event.button.button == SDL_BUTTON_LEFT) {
						if (windowWidth == 0 || windowHeight == 0) [[unlikely]] {
							break;
						}

						// Go from window positions to [0, 400) for x and [0, 480) for y
						const s32 x = (s32)std::round(event.button.x * 400.f / windowWidth);
						const s32 y = (s32)std::round(event.button.y * 480.f / windowHeight);

						// Check if touch falls in the touch screen area
						if (y >= 240 && y <= 480 && x >= 40 && x < 40 + 320) {
							// Convert to 3DS coordinates
							u16 x_converted = static_cast<u16>(x) - 40;
							u16 y_converted = static_cast<u16>(y) - 240;

							hid.setTouchScreenPress(x_converted, y_converted);
						} else {
							hid.releaseTouchScreen();
						}
					} else if (event.button.button == SDL_BUTTON_RIGHT) {
						holdingRightClick = true;
					}

					break;

				case SDL_MOUSEBUTTONUP:
					if (emu.romType == ROMType::None) break;

					if (event.button.button == SDL_BUTTON_LEFT) {
						hid.releaseTouchScreen();
					} else if (event.button.button == SDL_BUTTON_RIGHT) {
						holdingRightClick = false;
					}
					break;

				case SDL_CONTROLLERDEVICEADDED:
					if (gameController == nullptr) {
						gameController = SDL_GameControllerOpen(event.cdevice.which);
						gameControllerID = event.cdevice.which;

						setupControllerSensors(gameController);
					}
					break;

				case SDL_CONTROLLERDEVICEREMOVED:
					if (event.cdevice.which == gameControllerID) {
						SDL_GameControllerClose(gameController);
						gameController = nullptr;
						gameControllerID = 0;
					}
					break;

				case SDL_CONTROLLERBUTTONDOWN: {
					if (emu.romType == ROMType::None) break;
					u32 key = 0;
					switch (event.cbutton.button) {
						case SDL_CONTROLLER_BUTTON_A: key = Keys::B; break;
						case SDL_CONTROLLER_BUTTON_B: key = Keys::A; break;
						case SDL_CONTROLLER_BUTTON_X: key = Keys::Y; break;
						case SDL_CONTROLLER_BUTTON_Y: key = Keys::X; break;
						case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: key = Keys::L; break;
						case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: key = Keys::R; break;
						case SDL_CONTROLLER_BUTTON_DPAD_LEFT: key = Keys::Left; break;
						case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key = Keys::Right; break;
						case SDL_CONTROLLER_BUTTON_DPAD_UP: key = Keys::Up; break;
						case SDL_CONTROLLER_BUTTON_DPAD_DOWN: key = Keys::Down; break;
						case SDL_CONTROLLER_BUTTON_BACK: key = Keys::Select; break;
						case SDL_CONTROLLER_BUTTON_START: key = Keys::Start; break;
					}
					if (key != 0) {
						if (event.cbutton.state == SDL_PRESSED) {
							hid.pressKey(key);
						}
					}
					#ifdef __XBOX_BUILD
					if (!debugInfo) {
						if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
							startHeld = true;
							if (selectHeld) {
								overlayOpen = true;
								startHeld = selectHeld = false;
							}
						}
						else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
							selectHeld = true;
							if (startHeld) {
								overlayOpen = true;
								startHeld = selectHeld = false;
							}
						}
					}
					#endif
					break;
				}
				case SDL_CONTROLLERBUTTONUP: {
					if (emu.romType == ROMType::None) break;
					u32 key = 0;
					switch (event.cbutton.button) {
						case SDL_CONTROLLER_BUTTON_A: key = Keys::B; break;
						case SDL_CONTROLLER_BUTTON_B: key = Keys::A; break;
						case SDL_CONTROLLER_BUTTON_X: key = Keys::Y; break;
						case SDL_CONTROLLER_BUTTON_Y: key = Keys::X; break;
						case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: key = Keys::L; break;
						case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: key = Keys::R; break;
						case SDL_CONTROLLER_BUTTON_DPAD_LEFT: key = Keys::Left; break;
						case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: key = Keys::Right; break;
						case SDL_CONTROLLER_BUTTON_DPAD_UP: key = Keys::Up; break;
						case SDL_CONTROLLER_BUTTON_DPAD_DOWN: key = Keys::Down; break;
						case SDL_CONTROLLER_BUTTON_BACK: key = Keys::Select; break;
						case SDL_CONTROLLER_BUTTON_START: key = Keys::Start; break;
					}
					if (key != 0) {
						if (event.cbutton.state == SDL_RELEASED) {
							hid.releaseKey(key);
						}
					}
					#ifdef __XBOX_BUILD
					if (!debugInfo) {
						if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
							startHeld = false;
						}
						else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
							selectHeld = false;
						}
					}
					#endif
					break;
				}

				// Detect mouse motion events for gyroscope emulation
				case SDL_MOUSEMOTION: {
					if (emu.romType == ROMType::None) break;

					// Handle "dragging" across the touchscreen
					if (hid.isTouchScreenPressed()) {
						if (windowWidth == 0 || windowHeight == 0) [[unlikely]] {
							break;
						}

						// Go from window positions to [0, 400) for x and [0, 480) for y
						const s32 x = (s32)std::round(event.motion.x * 400.f / windowWidth);
						const s32 y = (s32)std::round(event.motion.y * 480.f / windowHeight);

						// Check if touch falls in the touch screen area and register the new touch screen position
						if (y >= 240 && y <= 480 && x >= 40 && x < 40 + 320) {
							// Convert to 3DS coordinates
							u16 x_converted = static_cast<u16>(x) - 40;
							u16 y_converted = static_cast<u16>(y) - 240;

							hid.setTouchScreenPress(x_converted, y_converted);
						}
					}

					// We use right click to indicate we want to rotate the console. If right click is not held, then this is not a gyroscope rotation
					if (holdingRightClick) {
						// Relative motion since last mouse motion event
						const s32 motionX = event.motion.xrel;
						const s32 motionY = event.motion.yrel;

						// The gyroscope involves lots of weird math I don't want to bother with atm
						// So up until then, we will set the gyroscope euler angles to fixed values based on the direction of the relative motion
						const s32 roll = motionX > 0 ? 0x7f : -0x7f;
						const s32 pitch = motionY > 0 ? 0x7f : -0x7f;
						hid.setRoll(roll);
						hid.setPitch(pitch);
					}
					break;
				}

				case SDL_CONTROLLERSENSORUPDATE: {
					if (event.csensor.sensor == SDL_SENSOR_GYRO) {
						auto rotation = Sensors::SDL::convertRotation({
							event.csensor.data[0],
							event.csensor.data[1],
							event.csensor.data[2],
						});

						hid.setPitch(s16(rotation.x));
						hid.setRoll(s16(rotation.y));
						hid.setYaw(s16(rotation.z));
					} else if (event.csensor.sensor == SDL_SENSOR_ACCEL) {
						auto accel = Sensors::SDL::convertAcceleration(event.csensor.data);
						hid.setAccel(accel.x, accel.y, accel.z);
					}
					break;
				}

				case SDL_DROPFILE: {
					char* droppedDir = event.drop.file;

					if (droppedDir) {
						const std::filesystem::path path(droppedDir);

						if (path.extension() == ".amiibo") {
							emu.loadAmiibo(path);
						} else if (path.extension() == ".lua") {
							emu.getLua().loadFile(droppedDir);
						} else {
							loadROM(path);
						}

						SDL_free(droppedDir);
					}
					break;
				}

				case SDL_WINDOWEVENT: {
					auto type = event.window.event;
					if (type == SDL_WINDOWEVENT_RESIZED) {
						windowWidth = event.window.data1;
						windowHeight = event.window.data2;
						emu.setOutputSize(windowWidth, windowHeight);
					}
				}
			}
		}


		#ifdef __XBOX_BUILD
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(window);
		ImGui::NewFrame();
	
		if (debugInfo) {
			ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.35f);
	
			ImGuiWindowFlags flags =
				  ImGuiWindowFlags_NoTitleBar
				| ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_AlwaysAutoResize
				| ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoNav
				| ImGuiWindowFlags_NoSavedSettings;
	
			ImGui::Begin("##DebugOverlay", nullptr, flags);
	
			int major = 0, minor = 0;
			glGetIntegerv(GL_MAJOR_VERSION, &major);
			glGetIntegerv(GL_MINOR_VERSION, &minor);
			const bool isGLES =
				strstr(reinterpret_cast<const char*>(glGetString(GL_VERSION)),
					   "OpenGL ES") != nullptr;
	
			ImGui::Text("Context : %s %d.%d", isGLES ? "GLES" : "GL", major, minor);
			ImGui::Text("Driver  : %s", glGetString(GL_RENDERER));
			ImGui::Text("FPS     : %.1f", ImGui::GetIO().Framerate);
			ImGui::Text("Platform: %s", SDL_GetPlatform());
	
			int curW, curH;
			SDL_GetWindowSize(window, &curW, &curH);
			ImGui::Text("Resolution: %dx%d", curW, curH);
	
			ImGui::End();
		}
	
		if (overlayOpen) {
			int winW, winH;
			SDL_GL_GetDrawableSize(window, &winW, &winH);
	
			ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_Always);
			ImGui::SetNextWindowPos(ImVec2(winW * 0.5f, winH * 0.5f),
									 ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	
			ImGui::Begin("##QuitOverlay", nullptr,
						 ImGuiWindowFlags_NoTitleBar     |
						 ImGuiWindowFlags_NoResize);
	
			char verLabel[32];
			snprintf(verLabel, sizeof(verLabel), "Panda3DS v%s", PANDA3DS_VERSION);
			ImVec2 txt = ImGui::CalcTextSize(verLabel);
			ImGui::SetCursorPosX((ImGui::GetWindowWidth() - txt.x) * 0.5f);
			ImGui::TextUnformatted(verLabel);
			ImGui::Dummy(ImVec2(0, 8));
	
			if (ImGui::Button("Quit to Main Menu", ImVec2(-1, 0))) {
				programRunning = false;
				overlayOpen = false;
			}
			if (ImGui::Button("Go Back", ImVec2(-1, 0))) {
				overlayOpen = false;
			}
			ImGui::End();
		}
	
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	#endif


		// Update controller analog sticks and HID service
		if (emu.romType != ROMType::None) {
			if (gameController != nullptr) {
				const s16 stickX = SDL_GameControllerGetAxis(gameController, SDL_CONTROLLER_AXIS_LEFTX);
				const s16 stickY = SDL_GameControllerGetAxis(gameController, SDL_CONTROLLER_AXIS_LEFTY);
				constexpr s16 deadzone = 3276;
				constexpr s16 maxValue = 0x9C;
				constexpr s16 div = 0x8000 / maxValue;

				// Avoid overriding the keyboard's circlepad input
				if (abs(stickX) < deadzone && !keyboardAnalogX) {
					hid.setCirclepadX(0);
				} else {
					hid.setCirclepadX(stickX / div);
				}

				if (abs(stickY) < deadzone && !keyboardAnalogY) {
					hid.setCirclepadY(0);
				} else {
					hid.setCirclepadY(-(stickY / div));
				}
			}

			hid.updateInputs(emu.getTicks());
		}
		// TODO: Should this be uncommented?
		// kernel.evalReschedule();

		SDL_GL_SwapWindow(window);
	}
}

void FrontendSDL::setupControllerSensors(SDL_GameController* controller) {
	bool haveGyro = SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) == SDL_TRUE;
	bool haveAccelerometer = SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) == SDL_TRUE;

	if (haveGyro) {
		SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE);
	}

	if (haveAccelerometer) {
		SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL, SDL_TRUE);
	}
}