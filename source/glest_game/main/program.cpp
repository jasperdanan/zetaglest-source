//
//      program.cpp:
//
//      This file is part of ZetaGlest <https://github.com/ZetaGlest>
//
//      Copyright (C) 2018  The ZetaGlest team
//
//      ZetaGlest is a fork of MegaGlest <https://megaglest.org>
//
//      This program is free software: you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation, either version 3 of the License, or
//      (at your option) any later version.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program.  If not, see <https://www.gnu.org/licenses/>

#include "program.h"

#include "sound.h"
#include "renderer.h"
#include "config.h"
#include "game.h"
#include "main_menu.h"
#include "intro.h"
#include "world.h"
#include "main.h"
#include "sound_renderer.h"
#include "logger.h"
#include "profiler.h"
#include "core_data.h"
#include "metrics.h"
#include "network_manager.h"
#include "menu_state_custom_game.h"
#include "menu_state_join_game.h"
#include "menu_state_scenario.h"
#include "leak_dumper.h"

using namespace
Shared::Util;
using
namespace
	Shared::Graphics;
using
namespace
	Shared::Graphics::Gl;

// =====================================================
//      class Program
// =====================================================

namespace Glest {
	namespace Game {

		const int
			Program::maxTimes = 10;
		Program *
			Program::singleton = NULL;
		const int
			SOUND_THREAD_UPDATE_MILLISECONDS = 25;

		bool Program::wantShutdownApplicationAfterGame = false;
		const char *
			ProgramState::MAIN_PROGRAM_RENDER_KEY = "ZETAGLEST.RENDER";

		// =====================================================
		//      class Program::CrashProgramState
		// =====================================================

		ProgramState::ProgramState(Program * program) {
			this->program = program;
			this->forceMouseRender = false;
			this->mouseX = 0;
			this->mouseY = 0;
			this->mouse2dAnim = 0;
			this->fps = 0;
			this->lastFps = 0;
			this->startX = 0;
			this->startY = 0;
		}

		void
			ProgramState::restoreToStartXY() {
			SDL_WarpMouseInWindow(this->program->getWindow()->getSDLWindow(),
				startX, startY);
		}

		void
			ProgramState::incrementFps() {
			fps++;
		}

		void
			ProgramState::tick() {
			lastFps = fps;
			fps = 0;
		}

		bool ProgramState::canRender(bool sleepIfCannotRender) {
			int
				maxFPSCap = Config::getInstance().getInt("RenderFPSCap", "500");
			int
				sleepMillis =
				Config::getInstance().getInt("RenderFPSCapSleepMillis", "1");
			//Renderer &renderer= Renderer::getInstance();
			if (GlobalStaticFlags::getIsNonGraphicalModeEnabled() == true) {
				maxFPSCap =
					Config::getInstance().getInt("RenderFPSCapHeadless", "250");
				sleepMillis =
					Config::getInstance().getInt("RenderFPSCapHeadlessSleepMillis",
						"1");
			}

			if (lastFps > maxFPSCap) {
				if (sleepIfCannotRender == true) {
					sleep(sleepMillis);
					//if(SystemFlags::VERBOSE_MODE_ENABLED) printf("In [%s::%s Line: %d] sleeping because lastFps = %d, maxFPSCap = %d sleepMillis = %d\n",extractFileFromDirectoryPath(__FILE__).c_str(),__FUNCTION__,__LINE__,lastFps,maxFPSCap,sleepMillis);
				}
				return false;
			}
			return true;
		}

		void
			ProgramState::render() {
			Renderer & renderer = Renderer::getInstance();

			canRender();
			incrementFps();

			if (GlobalStaticFlags::getIsNonGraphicalModeEnabled() == false) {
				renderer.clearBuffers();
				renderer.reset2d();
				renderer.renderMessageBox(program->getMsgBox());
				renderer.renderMouse2d(mouseX, mouseY, mouse2dAnim);
				renderer.swapBuffers();
			}
		}

		void
			ProgramState::update() {
			mouse2dAnim = (mouse2dAnim + 1) % Renderer::maxMouse2dAnim;
		}

		void
			ProgramState::mouseMove(int x, int y, const MouseState * mouseState) {
			mouseX = x;
			mouseY = y;
			program->getMsgBox()->mouseMove(x, y);
		}

		Program::ShowMessageProgramState::ShowMessageProgramState(Program *
			program,
			const char
			*msg) :
			ProgramState(program) {
			userWantsExit = false;
			msgBox.registerGraphicComponent("ShowMessageProgramState", "msgBox");
			msgBox.init("Ok");

			if (msg) {
				fprintf(stderr, "%s\n", msg);
				msgBox.setText(msg);
			} else {
				msgBox.setText("ZetaGlest has crashed.");
			}

			mouse2dAnim = mouseY = mouseX = 0;
			this->msg = (msg ? msg : "");
		}

		void
			Program::ShowMessageProgramState::render() {
			Renderer & renderer = Renderer::getInstance();
			renderer.clearBuffers();
			renderer.reset2d();
			renderer.renderMessageBox(&msgBox);
			renderer.renderMouse2d(mouseX, mouseY, mouse2dAnim);
			renderer.swapBuffers();
		}

		void
			Program::ShowMessageProgramState::mouseDownLeft(int x, int y) {
			int
				button = 0;
			if (msgBox.mouseClick(x, y, button)) {
				program->exit();
				userWantsExit = true;
			}
		}

		void
			Program::ShowMessageProgramState::keyPress(SDL_KeyboardEvent c) {
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s %d] c = [%d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__, __LINE__,
					c.keysym.sym);

			// if user pressed return we exit
			//if(c == 13) {
			if (isKeyPressed(SDLK_RETURN, c) == true) {
				program->exit();
				userWantsExit = true;
			}
		}

		void
			Program::ShowMessageProgramState::mouseMove(int x, int y,
				const MouseState *
				mouseState) {
			mouseX = x;
			mouseY = y;
			msgBox.mouseMove(x, y);
		}

		void
			Program::ShowMessageProgramState::update() {
			mouse2dAnim = (mouse2dAnim + 1) % Renderer::maxMouse2dAnim;
		}

		// ===================== PUBLIC ========================

		bool Program::rendererInitOk = false;
		bool Program::tryingRendererInit = false;

		Program::Program() :msgBox("Program", "msgBox") {
			//this->masterserverMode = false;
			this->window = NULL;
			this->shutdownApplicationEnabled = false;
			this->skipRenderFrameCount = 0;
			this->messageBoxIsSystemError = false;
			this->programStateOldSystemError = NULL;
			this->programState = NULL;
			this->singleton = this;
			this->soundThreadManager = NULL;

			//mesage box
			Lang & lang = Lang::getInstance();
			//msgBox.registerGraphicComponent("Program", "msgBox");
			msgBox.init(lang.getString("Ok"));
			msgBox.setEnabled(false);
		}

		//bool Program::isMasterserverMode() const {
		//      return this->masterserverMode;
		//}

		void
			Program::initNormal(WindowGl * window) {
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);
			Config & config = Config::getInstance();
			init(window);

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			setState(new Intro(this));
			showCursor(config.getBool("No2DMouseRendering", "false"));

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);
		}

		void
			Program::initSavedGame(WindowGl * window, bool masterserverMode,
				string saveGameFile) {
			init(window);
			MainMenu *
				mainMenu = new MainMenu(this);
			setState(mainMenu);

			if (saveGameFile == "") {
				Config & config = Config::getInstance();
				saveGameFile = config.getString("LastSavedGame", "");
				if (saveGameFile == "") {
					saveGameFile = GameConstants::saveGameFileDefault;
					if (getGameReadWritePath(GameConstants::path_logs_CacheLookupKey)
						!= "") {
						saveGameFile =
							getGameReadWritePath(GameConstants::path_logs_CacheLookupKey) +
							saveGameFile;
					} else {
						string
							userData =
							Config::getInstance().getString("UserData_Root", "");
						if (userData != "") {
							endPathWithSlash(userData);
						}
						saveGameFile = userData + saveGameFile;
					}
				}
			}

			//if(SystemFlags::VERBOSE_MODE_ENABLED) printf("Loading game from [%s]\n",saveGameFile.c_str());
			printf("Loading saved game from [%s]\n", saveGameFile.c_str());

			Game::loadGame(saveGameFile, this, masterserverMode);
		}

		void
			Program::initServer(WindowGl * window, bool autostart,
				bool openNetworkSlots, bool masterserverMode) {
			//this->masterserverMode = masterserverMode;
			init(window);
			MainMenu *
				mainMenu = new MainMenu(this);
			setState(mainMenu);
			mainMenu->setState(new
				MenuStateCustomGame(this, mainMenu,
					openNetworkSlots, pNewGame,
					autostart, NULL,
					masterserverMode));
		}

		void
			Program::initServer(WindowGl * window, GameSettings * settings) {
			init(window);
			MainMenu *
				mainMenu = new MainMenu(this);
			setState(mainMenu);
			mainMenu->setState(new
				MenuStateCustomGame(this, mainMenu, false,
					pNewGame, true, settings));
		}

		void
			Program::initClient(WindowGl * window, const Ip & serverIp,
				int portNumber) {
			init(window);
			MainMenu *
				mainMenu = new MainMenu(this);
			setState(mainMenu);
			mainMenu->setState(new
				MenuStateJoinGame(this, mainMenu, true, serverIp,
					portNumber));
		}

		void
			Program::initClientAutoFindHost(WindowGl * window) {
			init(window);
			MainMenu *
				mainMenu = new MainMenu(this);
			setState(mainMenu);
			bool autoFindHost = true;
			mainMenu->setState(new
				MenuStateJoinGame(this, mainMenu, &autoFindHost));

		}

		void
			Program::initScenario(WindowGl * window, string autoloadScenarioName) {
			init(window);
			MainMenu *
				mainMenu = new MainMenu(this);
			setState(mainMenu);
			mainMenu->setState(new MenuStateScenario(this, mainMenu, false,
				Config::
				getInstance
				().getPathListForType
				(ptScenarios),
				autoloadScenarioName));
		}

		Program::~Program() {
			if (SystemFlags::VERBOSE_MODE_ENABLED)
				printf("In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__);
			delete programState;
			programState = NULL;

			if (SystemFlags::VERBOSE_MODE_ENABLED)
				printf("In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__);

			Renderer::getInstance().end();
			CoreData & coreData = CoreData::getInstance();
			coreData.cleanup();

			if (SystemFlags::VERBOSE_MODE_ENABLED)
				printf("In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__);

			//restore video mode
			restoreDisplaySettings();
			singleton = NULL;

			if (SystemFlags::VERBOSE_MODE_ENABLED)
				printf("In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__);

			if (soundThreadManager != NULL) {
				BaseThread::shutdownAndWait(soundThreadManager);
				delete soundThreadManager;
				soundThreadManager = NULL;
			}

			if (SystemFlags::VERBOSE_MODE_ENABLED)
				printf("In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__);
		}

		void
			Program::restoreStateFromSystemError() {
			messageBoxIsSystemError = false;
			if (this->programStateOldSystemError == NULL) {
				setState(new Intro(this));
			} else {
				setState(this->programStateOldSystemError);
			}
		}

		bool Program::textInput(std::string text) {
			if (msgBox.getEnabled()) {
				return false;
			}
			//delegate event
			return programState->textInput(text);
		}

		bool Program::sdlKeyDown(SDL_KeyboardEvent key) {
			//delegate event
			return programState->sdlKeyDown(key);
		}

		void
			Program::keyDown(SDL_KeyboardEvent key) {
			if (msgBox.getEnabled()) {
				//SDL_keysym keystate = Window::getKeystate();
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d]\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__);

				//if(key == vkEscape  || key == SDLK_ESCAPE ||
				//      ((key == vkReturn || key == SDLK_RETURN || key == SDLK_KP_ENTER) && !(keystate.mod & (KMOD_LALT | KMOD_RALT)))) {
				if (isKeyPressed(SDLK_ESCAPE, key) == true
					|| ((isKeyPressed(SDLK_RETURN, key) == true)
						&& !(key.keysym.mod & (KMOD_LALT | KMOD_RALT)))) {
					if (SystemFlags::
						getSystemSettingType(SystemFlags::debugSystem).enabled)
						SystemFlags::OutputDebug(SystemFlags::debugSystem,
							"In [%s::%s Line: %d]\n",
							extractFileFromDirectoryPath
							(__FILE__).c_str(), __FUNCTION__,
							__LINE__);

					//printf("---> keystate [%d]\n",keystate);
					msgBox.setEnabled(false);
					if (messageBoxIsSystemError == true) {
						restoreStateFromSystemError();
					}
				}
			}
			//delegate event
			programState->keyDown(key);
		}

		void
			Program::keyUp(SDL_KeyboardEvent key) {
			programState->keyUp(key);
		}

		void
			Program::keyPress(SDL_KeyboardEvent c) {
			programState->keyPress(c);
		}

		void
			Program::mouseDownLeft(int x, int y) {
			if (msgBox.getEnabled()) {
				int
					button = 0;
				if (msgBox.mouseClick(x, y, button)) {
					//if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",extractFileFromDirectoryPath(__FILE__).c_str(),__FUNCTION__,__LINE__);
					//close message box
					msgBox.setEnabled(false);
					if (messageBoxIsSystemError == true) {
						restoreStateFromSystemError();
					}
				}
			}
		}

		void
			Program::eventMouseMove(int x, int y, const MouseState * ms) {
			if (msgBox.getEnabled()) {
				msgBox.mouseMove(x, y);
			}
		}

		void
			Program::simpleTask(BaseThread * callingThread, void *userdata) {
			loopWorker();
		}

		void
			Program::loop() {
			loopWorker();
		}

		void
			Program::loopWorker() {
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugPerformance,
					"In [%s::%s Line: %d] ================================= MAIN LOOP START ================================= \n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//Renderer &renderer= Renderer::getInstance();
			if (GlobalStaticFlags::getIsNonGraphicalModeEnabled() == false
				&& window) {
				MainWindow *
					mainWindow = dynamic_cast <MainWindow *>(window);
				if (mainWindow) {
					//mainWindow->render();
					if (mainWindow->getTriggerLanguageToggle()) {
						mainWindow->toggleLanguage(mainWindow->getTriggerLanguage());
					}
				}
			}

			Chrono chronoPerformanceCounts;

			bool
				showPerfStats =
				Config::getInstance().getBool("ShowPerfStats", "false");
			Chrono chronoPerf;
			char
				perfBuf[8096] = "";
			std::vector < string > perfList;
			if (showPerfStats)
				chronoPerf.start();

			Chrono chronoLoop;

#ifdef DEBUG
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled)
				chronoLoop.start();
#endif

			Chrono chrono;

#ifdef DEBUG
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled)
				chrono.start();
#endif

			//render
			assert(programState != NULL);

			if (this->programState->quitTriggered() == true) {
#ifdef DEBUG
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d]\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__);
#endif

				Stats endStats = this->programState->quitAndToggleState();
#ifdef DEBUG
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d]\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__);
#endif


				Game::exitGameState(this, endStats);
#ifdef DEBUG
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d]\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__);
#endif

				return;
			}
			ProgramState *
				prevState = this->programState;

			if (showPerfStats) {
				sprintf(perfBuf,
					"In [%s::%s] Line: %d took msecs: " MG_I64_SPECIFIER "\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__, chronoPerf.getMillis());
				perfList.push_back(perfBuf);
			}

			assert(programState != NULL);

			chronoPerformanceCounts.start();

			programState->render();

			programState->addPerformanceCount(ProgramState::
				MAIN_PROGRAM_RENDER_KEY,
				chronoPerformanceCounts.
				getMillis());

			if (showPerfStats) {
				sprintf(perfBuf,
					"In [%s::%s] Line: %d took msecs: " MG_I64_SPECIFIER "\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__, chronoPerf.getMillis());
				perfList.push_back(perfBuf);
			}
#ifdef DEBUG
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled
				&& chrono.getMillis() > 0)
				SystemFlags::OutputDebug(SystemFlags::debugPerformance,
					"In [%s::%s] Line: %d programState->render took msecs: %lld ==============> MAIN LOOP RENDERING\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__, __LINE__,
					chrono.getMillis());
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled
				&& chrono.getMillis() > 0)
				chrono.start();

			//update camera
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled)
				chrono.start();
#endif

			chronoPerformanceCounts.start();

			while (updateCameraTimer.isTime()) {
				programState->updateCamera();
			}

			programState->addPerformanceCount("programState->updateCamera()",
				chronoPerformanceCounts.getMillis
				());

			if (showPerfStats) {
				sprintf(perfBuf,
					"In [%s::%s] Line: %d took msecs: " MG_I64_SPECIFIER "\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__, chronoPerf.getMillis());
				perfList.push_back(perfBuf);
			}

#ifdef DEBUG
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled
				&& chrono.getMillis() > 0)
				SystemFlags::OutputDebug(SystemFlags::debugPerformance,
					"In [%s::%s] Line: %d programState->render took msecs: %lld ==============> MAIN LOOP CAMERA UPDATING\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__, __LINE__,
					chrono.getMillis());
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled
				&& chrono.getMillis() > 0)
				chrono.start();


			//update world
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled)
				chrono.start();
#endif
			int
				updateCount = 0;
			while (prevState == this->programState && updateTimer.isTime()) {
				Chrono chronoUpdateLoop;

#ifdef DEBUG
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugPerformance).enabled)
					chronoUpdateLoop.start();
#endif
				if (showPerfStats) {
					sprintf(perfBuf,
						"In [%s::%s] Line: %d took msecs: " MG_I64_SPECIFIER
						" updateCount: %d\n",
						extractFileFromDirectoryPath(__FILE__).c_str(),
						__FUNCTION__, __LINE__, chronoPerf.getMillis(),
						updateCount);
					perfList.push_back(perfBuf);
				}

				GraphicComponent::update();
				programState->update();

#ifdef DEBUG
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugPerformance).enabled
					&& chronoUpdateLoop.getMillis() > 0)
					SystemFlags::OutputDebug(SystemFlags::debugPerformance,
						"In [%s::%s Line: %d] programState->update took msecs: %lld, updateCount = %d\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__, chronoUpdateLoop.getMillis(),
						updateCount);
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugPerformance).enabled
					&& chronoUpdateLoop.getMillis() > 0)
					chronoUpdateLoop.start();
#endif

				if (showPerfStats) {
					sprintf(perfBuf,
						"In [%s::%s] Line: %d took msecs: " MG_I64_SPECIFIER
						" updateCount: %d\n",
						extractFileFromDirectoryPath(__FILE__).c_str(),
						__FUNCTION__, __LINE__, chronoPerf.getMillis(),
						updateCount);
					perfList.push_back(perfBuf);
				}

				if (prevState == this->programState) {
					chronoPerformanceCounts.start();

					if (soundThreadManager == NULL
						|| soundThreadManager->isThreadExecutionLagging()) {
						if (soundThreadManager != NULL) {


							if (SystemFlags::getSystemSettingType
							(SystemFlags::debugSystem).enabled)
								SystemFlags::OutputDebug(SystemFlags::debugSystem,
									"In [%s::%s %d] ERROR / WARNING soundThreadManager->isThreadExecutionLagging is TRUE\n",
									extractFileFromDirectoryPath
									(__FILE__).c_str(), __FUNCTION__,
									__LINE__);
							SystemFlags::OutputDebug(SystemFlags::debugError,
								"In [%s::%s %d] ERROR / WARNING soundThreadManager->isThreadExecutionLagging is TRUE\n",
								extractFileFromDirectoryPath
								(__FILE__).c_str(), __FUNCTION__,
								__LINE__);
						}
						SoundRenderer::getInstance().update();
#ifdef DEBUG
						if (SystemFlags::getSystemSettingType
						(SystemFlags::debugPerformance).enabled
							&& chronoUpdateLoop.getMillis() > 0)
							SystemFlags::OutputDebug(SystemFlags::debugPerformance,
								"In [%s::%s Line: %d] SoundRenderer::getInstance().update() took msecs: %lld, updateCount = %d\n",
								extractFileFromDirectoryPath
								(__FILE__).c_str(), __FUNCTION__,
								__LINE__,
								chronoUpdateLoop.getMillis(),
								updateCount);
						if (SystemFlags::getSystemSettingType
						(SystemFlags::debugPerformance).enabled
							&& chronoUpdateLoop.getMillis() > 0)
							chronoUpdateLoop.start();
#endif
					}


					programState->addPerformanceCount
					("SoundRenderer::getInstance().update()",
						chronoPerformanceCounts.getMillis());

					if (showPerfStats) {
						sprintf(perfBuf,
							"In [%s::%s] Line: %d took msecs: " MG_I64_SPECIFIER
							" updateCount: %d\n",
							extractFileFromDirectoryPath(__FILE__).c_str(),
							__FUNCTION__, __LINE__, chronoPerf.getMillis(),
							updateCount);
						perfList.push_back(perfBuf);
					}

					chronoPerformanceCounts.start();

					NetworkManager::getInstance().update();

					programState->addPerformanceCount
					("NetworkManager::getInstance().update()",
						chronoPerformanceCounts.getMillis());
#ifdef DEBUG
					if (SystemFlags::getSystemSettingType
					(SystemFlags::debugPerformance).enabled
						&& chronoUpdateLoop.getMillis() > 0)
						SystemFlags::OutputDebug(SystemFlags::debugPerformance,
							"In [%s::%s Line: %d] NetworkManager::getInstance().update() took msecs: %lld, updateCount = %d\n",
							extractFileFromDirectoryPath
							(__FILE__).c_str(), __FUNCTION__,
							__LINE__, chronoUpdateLoop.getMillis(),
							updateCount);
					if (SystemFlags::getSystemSettingType
					(SystemFlags::debugPerformance).enabled
						&& chronoUpdateLoop.getMillis() > 0)
						chronoUpdateLoop.start();
#endif

					if (showPerfStats) {
						sprintf(perfBuf,
							"In [%s::%s] Line: %d took msecs: " MG_I64_SPECIFIER
							" updateCount: %d\n",
							extractFileFromDirectoryPath(__FILE__).c_str(),
							__FUNCTION__, __LINE__, chronoPerf.getMillis(),
							updateCount);
						perfList.push_back(perfBuf);
					}

				}
				updateCount++;
			}

#ifdef DEBUG
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled
				&& chrono.getMillis() > 0)
				SystemFlags::OutputDebug(SystemFlags::debugPerformance,
					"In [%s::%s] Line: %d AFTER programState->update took msecs: %lld ==============> MAIN LOOP BODY LOGIC, updateCount = %d\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__, __LINE__,
					chrono.getMillis(), updateCount);
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled
				&& chrono.getMillis() > 0)
				chrono.start();

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled
				&& chrono.getMillis() > 0)
				chrono.start();
#endif

			if (showPerfStats) {
				sprintf(perfBuf,
					"In [%s::%s] Line: %d took msecs: " MG_I64_SPECIFIER "\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__, chronoPerf.getMillis());
				perfList.push_back(perfBuf);
			}

			if (prevState == this->programState) {
				//fps timer
				chronoPerformanceCounts.start();

				chrono.start();
				while (fpsTimer.isTime()) {
					programState->tick();
				}

				programState->addPerformanceCount("programState->tick()",
					chronoPerformanceCounts.getMillis
					());

				if (showPerfStats) {
					sprintf(perfBuf,
						"In [%s::%s] Line: %d took msecs: " MG_I64_SPECIFIER "\n",
						extractFileFromDirectoryPath(__FILE__).c_str(),
						__FUNCTION__, __LINE__, chronoPerf.getMillis());
					perfList.push_back(perfBuf);
				}
#ifdef DEBUG
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugPerformance).enabled
					&& chrono.getMillis() > 0)
					SystemFlags::OutputDebug(SystemFlags::debugPerformance,
						"In [%s::%s] Line: %d programState->render took msecs: %lld ==============> MAIN LOOP TICKING\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__, chrono.getMillis());
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugPerformance).enabled
					&& chrono.getMillis() > 0)
					chrono.start();

				//if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled && chrono.getMillis() > 0) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s] Line: %d programState->render took msecs: %lld ==============> MAIN LOOP TICKING\n",extractFileFromDirectoryPath(__FILE__).c_str(),__FUNCTION__,__LINE__,chrono.getMillis());
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugPerformance).enabled
					&& chrono.getMillis() > 0)
					chrono.start();
#endif

			}

			if (showPerfStats && chronoPerf.getMillis() >= 100) {
				for (unsigned int x = 0; x < perfList.size(); ++x) {
					printf("%s", perfList[x].c_str());
				}
			}
#ifdef DEBUG
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugPerformance).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugPerformance,
					"In [%s::%s Line: %d] ------------------------------- MAIN LOOP END, stats: loop took msecs: %lld -------------------------------\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__, __LINE__,
					chronoLoop.getMillis());
#endif
		}

		void
			Program::resize(SizeState sizeState) {

			switch (sizeState) {
				case ssMinimized:
					//restoreVideoMode();
					break;
				case ssMaximized:
				case ssRestored:
					//setDisplaySettings();
					//renderer.reloadResources();
					break;
			}
		}

		// ==================== misc ====================

		void
			Program::renderProgramMsgBox() {
			Renderer & renderer = Renderer::getInstance();
			if (msgBox.getEnabled()) {
				renderer.renderMessageBox(&msgBox);
			}

			if (GlobalStaticFlags::getIsNonGraphicalModeEnabled() == false
				&& window) {
				MainWindow *
					mainWindow = dynamic_cast <MainWindow *>(window);
				if (mainWindow) {
					mainWindow->render();
				}
			}

		}

		void
			Program::setState(ProgramState * programStateNew, bool cleanupOldState) {
			try {
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d]\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__);

				this->programStateOldSystemError = this->programState;
				bool msgBoxEnabled = msgBox.getEnabled();

				if (dynamic_cast <Game *>(programStateNew) != NULL) {
					if (SystemFlags::
						getSystemSettingType(SystemFlags::debugSystem).enabled)
						SystemFlags::OutputDebug(SystemFlags::debugSystem,
							"In [%s::%s Line: %d]\n",
							extractFileFromDirectoryPath
							(__FILE__).c_str(), __FUNCTION__,
							__LINE__);

					int
						X = 0;
					int
						Y = 0;
					SDL_GetMouseState(&X, &Y);
					programStateNew->setStartXY(X, Y);
					Logger::getInstance().setProgress(0);
					Logger::getInstance().setState("");


					SDL_PumpEvents();

					showCursor(true);
					SDL_PumpEvents();
					sleep(0);
					if (SystemFlags::
						getSystemSettingType(SystemFlags::debugSystem).enabled)
						SystemFlags::OutputDebug(SystemFlags::debugSystem,
							"In [%s::%s Line: %d]\n",
							extractFileFromDirectoryPath
							(__FILE__).c_str(), __FUNCTION__,
							__LINE__);
				}

				if (cleanupOldState == true) {
					if (this->programState != programStateNew) {
						if (SystemFlags::
							getSystemSettingType(SystemFlags::debugSystem).enabled)
							SystemFlags::OutputDebug(SystemFlags::debugSystem,
								"In [%s::%s Line: %d]\n",
								extractFileFromDirectoryPath
								(__FILE__).c_str(), __FUNCTION__,
								__LINE__);

						this->programStateOldSystemError = NULL;
						delete this->programState;

						if (SystemFlags::
							getSystemSettingType(SystemFlags::debugSystem).enabled)
							SystemFlags::OutputDebug(SystemFlags::debugSystem,
								"In [%s::%s Line: %d]\n",
								extractFileFromDirectoryPath
								(__FILE__).c_str(), __FUNCTION__,
								__LINE__);

						this->programState = NULL;

						if (SystemFlags::
							getSystemSettingType(SystemFlags::debugSystem).enabled)
							SystemFlags::OutputDebug(SystemFlags::debugSystem,
								"In [%s::%s Line: %d]\n",
								extractFileFromDirectoryPath
								(__FILE__).c_str(), __FUNCTION__,
								__LINE__);
					}
				}

				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d]\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__);

				//mesage box
				Lang & lang = Lang::getInstance();
				msgBox.init(lang.getString("Ok"));
				msgBox.setEnabled(msgBoxEnabled);

				fpsTimer.init(1, maxTimes);
				updateTimer.init(GameConstants::updateFps, maxTimes);
				updateCameraTimer.init(GameConstants::cameraFps, maxTimes);

				this->programState = programStateNew;
				assert(programStateNew != NULL);
				if (programStateNew == NULL) {
					throw megaglest_runtime_error("programStateNew == NULL");
				}
				programStateNew->load();

				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d]\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__);

				programStateNew->init();

				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d]\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__);

				updateTimer.reset();
				updateCameraTimer.reset();
				fpsTimer.reset();

				Config & config = Config::getInstance();
				if (dynamic_cast <Intro *>(programStateNew) != NULL
					&& msgBoxEnabled == true) {
					showCursor(true);
				} else {
					showCursor(config.getBool("No2DMouseRendering", "false"));
				}

				this->programStateOldSystemError = NULL;
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d]\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__);
			} catch (megaglest_runtime_error & e) {
				//printf("3333333 ex.wantStackTrace() = %d\n",e.wantStackTrace());
				char
					szBuf[8096] = "";
				snprintf(szBuf, 8096, "In [%s::%s Line: %d]\nError [%s]\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__, e.what());
				SystemFlags::OutputDebug(SystemFlags::debugError, szBuf);
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem, szBuf);
				//abort();
				//printf("44444444a ex.wantStackTrace() = %d\n",e.wantStackTrace());

				messageBoxIsSystemError = true;
				string errorMsg = e.what();

				if (GlobalStaticFlags::getIsNonGraphicalModeEnabled() == false) {
					if (dynamic_cast <Game *>(programStateNew) != NULL) {
						try {
							Game *
								game = dynamic_cast <Game *>(programStateNew);
							Renderer & renderer = Renderer::getInstance();
							game->setQuitPendingIndicator(); // by this the world is no more updated
							renderer.initGame(game, game->getGameCameraPtr());
						} catch (megaglest_runtime_error & ex2) {
							errorMsg += "\n" + string(ex2.what());
						}
					}
				}

				//printf("44444444b ex.wantStackTrace() = %d\n",e.wantStackTrace());
				this->showMessage(errorMsg.c_str());
				//setState(new Intro(this));
			} catch (const exception & e) {
				char
					szBuf[8096] = "";
				snprintf(szBuf, 8096, "In [%s::%s Line: %d]\nError [%s]\n",
					extractFileFromDirectoryPath(__FILE__).c_str(),
					__FUNCTION__, __LINE__, e.what());
				SystemFlags::OutputDebug(SystemFlags::debugError, szBuf);
				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem, szBuf);
				//abort();

				messageBoxIsSystemError = true;

				if (GlobalStaticFlags::getIsNonGraphicalModeEnabled() == false) {
					if (dynamic_cast <Game *>(programStateNew) != NULL) {
						Game *
							game = dynamic_cast <Game *>(programStateNew);
						Renderer & renderer = Renderer::getInstance();
						renderer.initGame(game, game->getGameCameraPtr());
					}
				}

				this->showMessage(e.what());
				//setState(new Intro(this));
			}
		}

		void
			Program::exit() {
			window->destroy();
		}

		// ==================== PRIVATE ====================

		void
			Program::initResolution() {
			const
				Metrics &
				metrics = Metrics::getInstance();
			if (window->getScreenWidth() != metrics.getScreenW() ||
				window->getScreenHeight() != metrics.getScreenH()) {

				int
					oldW = metrics.getScreenW();
				int
					oldH = metrics.getScreenH();

				Config & config = Config::getInstance();
				config.setInt("ScreenWidth", window->getScreenWidth(), true);
				config.setInt("ScreenHeight", window->getScreenHeight(), true);

				metrics.reload(window->getScreenWidth(),
					window->getScreenHeight());
				printf
				("MainWindow forced change of resolution to desktop values (%d x %d) instead of (%d x %d)\n",
					metrics.getScreenW(), metrics.getScreenH(), oldW, oldH);
			}
		}

		void
			Program::init(WindowGl * window, bool initSound, bool toggleFullScreen) {
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			this->window = window;
			Config & config = Config::getInstance();

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//set video mode
			if (toggleFullScreen == false) {
				setDisplaySettings();
			}

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//window
			//window->setText("ZetaGlest");
			window->setStyle(config.getBool("Windowed") ? wsWindowedFixed :
				wsFullscreen);
			window->setPos(0, 0);
			window->setSize(config.getInt("ScreenWidth"),
				config.getInt("ScreenHeight"));
			window->create();

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//timers
			fpsTimer.init(1, maxTimes);
			updateTimer.init(GameConstants::updateFps, maxTimes);
			updateCameraTimer.init(GameConstants::cameraFps, maxTimes);

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//log start
			Logger & logger = Logger::getInstance();
			string logFile = "glest.log";
			if (getGameReadWritePath(GameConstants::path_logs_CacheLookupKey) !=
				"") {
				logFile =
					getGameReadWritePath(GameConstants::path_logs_CacheLookupKey) +
					logFile;
			} else {
				string userData = config.getString("UserData_Root", "");
				if (userData != "") {
					endPathWithSlash(userData);
				}

				logFile = userData + logFile;
			}
			logger.setFile(logFile);
			logger.clear();

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//lang
			//Lang &lang= Lang::getInstance();
			Lang::getInstance();

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//render
			Renderer & renderer = Renderer::getInstance();

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			window->initGl(config.getInt("ColorBits"),
				config.getInt("DepthBits"),
				config.getInt("StencilBits"),
				config.getBool("HardwareAcceleration", "false"),
				config.getBool("FullScreenAntiAliasing", "0"),
				config.getFloat("GammaValue", "0.0"));
			window->setText(config.getString("WindowTitle", "ZetaGlest"));
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			window->makeCurrentGl();
			initResolution();

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//coreData, needs renderer, but must load before renderer init
			CoreData & coreData = CoreData::getInstance();
			coreData.load();

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//init renderer (load global textures)
			tryingRendererInit = true;
			renderer.init();
			tryingRendererInit = false;
			rendererInitOk = true;

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);

			//sound
			if (initSound == true && toggleFullScreen == false) {
				SoundRenderer & soundRenderer = SoundRenderer::getInstance();
				bool initOk = soundRenderer.init(window);

				if (SystemFlags::
					getSystemSettingType(SystemFlags::debugSystem).enabled)
					SystemFlags::OutputDebug(SystemFlags::debugSystem,
						"In [%s::%s Line: %d] initOk = %d\n",
						extractFileFromDirectoryPath
						(__FILE__).c_str(), __FUNCTION__,
						__LINE__, initOk);

				// Test sound system failed
				//initOk = false;
				// END

				if (initOk == false) {
					string sError = "Sound System could not be initialized!";
					this->showMessage(sError.c_str());
				}

				// Run sound streaming in a background thread if enabled
				if (SoundRenderer::getInstance().runningThreaded() == true) {
					if (BaseThread::shutdownAndWait(soundThreadManager) == true) {
						delete soundThreadManager;
					}
					static
						string
						mutexOwnerId =
						string(extractFileFromDirectoryPath(__FILE__).c_str()) +
						string("_") + intToStr(__LINE__);
					soundThreadManager =
						new SimpleTaskThread(&SoundRenderer::getInstance(), 0,
							SOUND_THREAD_UPDATE_MILLISECONDS);
					soundThreadManager->setUniqueID(mutexOwnerId);
					soundThreadManager->start();
				}
			}

			NetworkInterface::setAllowGameDataSynchCheck(Config::
				getInstance().getBool
				("AllowGameDataSynchCheck",
					"false"));
			NetworkInterface::setAllowDownloadDataSynch(Config::
				getInstance().getBool
				("AllowDownloadDataSynch",
					"false"));

			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s Line: %d]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__,
					__LINE__);
		}

		void
			Program::reloadUI() {
			if (programState) {
				programState->reloadUI();
			}
		}

		void
			Program::setDisplaySettings() {

			Config & config = Config::getInstance();

			if (!config.getBool("Windowed")) {

				//int freq= config.getInt("RefreshFrequency");
				int
					colorBits = config.getInt("ColorBits");
				int
					screenWidth = config.getInt("ScreenWidth");
				int
					screenHeight = config.getInt("ScreenHeight");

				if (config.getBool("AutoMaxFullScreen", "false") == true) {
					getFullscreenVideoInfo(colorBits, screenWidth, screenHeight,
						!config.getBool("Windowed"));
					config.setInt("ColorBits", colorBits);
					config.setInt("ScreenWidth", screenWidth);
					config.setInt("ScreenHeight", screenHeight);
				}
			}
			changeVideoModeFullScreen(!config.getBool("Windowed"));
		}

		void
			Program::restoreDisplaySettings() {
			Config & config = Config::getInstance();

			if (!config.getBool("Windowed")) {
				restoreVideoMode(this->getWindow()->getSDLWindow());
			}
		}

		bool Program::isMessageShowing() {
			return msgBox.getEnabled();
		}

		void
			Program::showMessage(const char *msg) {
			if (SystemFlags::
				getSystemSettingType(SystemFlags::debugSystem).enabled)
				SystemFlags::OutputDebug(SystemFlags::debugSystem,
					"In [%s::%s %d] msg [%s]\n",
					extractFileFromDirectoryPath
					(__FILE__).c_str(), __FUNCTION__, __LINE__,
					msg);

			msgBox.setText(msg);
			msgBox.setEnabled(true);

			if (GlobalStaticFlags::getIsNonGraphicalModeEnabled() == true) {
				printf("Message:\n%s\n", msg);

				if (messageBoxIsSystemError == true) {
					messageBoxIsSystemError = false;
					//setState(new Intro(this));
					initServer(window, false, true, true);
				}
			}
		}

		void
			Program::stopSoundSystem() {
			if (soundThreadManager != NULL) {
				BaseThread::shutdownAndWait(soundThreadManager);
				delete soundThreadManager;
				soundThreadManager = NULL;
			}
		}

		void
			Program::startSoundSystem() {
			stopSoundSystem();
			if (SoundRenderer::getInstance().runningThreaded() == true) {
				static
					string
					mutexOwnerId =
					string(extractFileFromDirectoryPath(__FILE__).c_str()) +
					string("_") + intToStr(__LINE__);
				soundThreadManager =
					new SimpleTaskThread(&SoundRenderer::getInstance(), 0,
						SOUND_THREAD_UPDATE_MILLISECONDS);
				soundThreadManager->setUniqueID(mutexOwnerId);
				soundThreadManager->start();
			}
		}

		//void Program::resetSoundSystem() {
		//      startSoundSystem();
		//}

		//void Program::reInitGl() {
		//      if(window != NULL) {
		//              Config &config= Config::getInstance();
		//              window->initGl(config.getInt("ColorBits"),
		//                                     config.getInt("DepthBits"),
		//                                     config.getInt("StencilBits"),
		//                                     config.getBool("HardwareAcceleration","false"),
		//                                     config.getBool("FullScreenAntiAliasing","false"),
		//                                     config.getFloat("GammaValue","0.0"));
		//              window->setText(config.getString("WindowTitle","ZetaGlest"));
		//              initResolution();
		//      }
		//}

		void
			Program::consoleAddLine(string line) {
			if (programState != NULL) {
				programState->consoleAddLine(line);
			}
		}

		SimpleTaskThread *
			Program::getSoundThreadManager(bool takeOwnership) {
			SimpleTaskThread *
				result = soundThreadManager;
			if (takeOwnership) {
				soundThreadManager = NULL;
			}
			return result;
		}

	}
}                               //end namespace
