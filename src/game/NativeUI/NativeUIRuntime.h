#pragma once
// The native UI runtime as native screens and overlays see it (RmlUi types). The game uses NativeUI.h.

#include "NativeUI.h"

#include <RmlUi/Core.h>

#include <functional>
#include <memory>
#include <string>

namespace NativeUI
{
	/** Starts the runtime if it can run (see Available); true if running. */
	bool Start();
	Rml::Context* Context();
	/** Loads <ui dir>/<path>. The markup may use var(--token). Throws if it cannot be loaded. */
	Rml::ElementDocument* LoadDocument(std::string const& path);
	void CloseDocument(Rml::ElementDocument*);
	/** Something changed that input and data binding did not tell the runtime about (a timer, a scroll set from
	 * code): draw again for the next @a frames frames (software path; the GPU path draws every frame). */
	void Invalidate(int frames = 30);
	/** A string of the native UI table (assets/ui/strings/<lang>.json, English as fallback); the key if missing. */
	std::string Str(std::string const& key);
	/** Escapes text for RML. */
	std::string Escape(std::string const& text);
	/** The element under the mouse (or nullptr), and the mouse position in output pixels. */
	Rml::Vector2f MousePosition();

	/** A native screen: owns input and drawing while it is the current screen. */
	class Screen
	{
	public:
		virtual ~Screen() = default;
		virtual void Enter() = 0;
		/** One frame: handle the queued input, update; return the next screen (the own id to stay). */
		virtual ScreenID Handle() = 0;
		virtual void Exit() = 0;
		/** The output size or UI scale changed (the context has its new size already). */
		virtual void Resized() {}
		/** The screen is done without changing the game's screen (a design mock): the runtime closes it and the legacy
		 * screen underneath runs again. */
		virtual bool Finished() const { return false; }
		/** The legacy screen underneath is showing something the native screen does not draw (a legacy-only popup):
		 * the runtime hides nothing but gives the mouse to the legacy regions, and the screen hides its document. */
		virtual bool PassThrough() const { return false; }
	};

	/** Native screen factories (NativeUI.cc keeps the table). */
	std::unique_ptr<Screen> CreateCreditsScreen();
	std::unique_ptr<Screen> CreateMainMenuScreen();
	std::unique_ptr<Screen> CreateOptionsScreen();
	std::unique_ptr<Screen> CreateSaveLoadScreen();
	std::unique_ptr<Screen> CreateNewGameScreen();
	std::unique_ptr<Screen> CreateMapScreen();
	std::unique_ptr<Screen> CreateAutoResolveScreen();
	std::unique_ptr<Screen> CreateShopKeeperScreen();
	std::unique_ptr<Screen> CreateLaptopScreen();
	/** The pre-game setup screen (FrontSetup.cc); @a engineOptions is the EngineOptions* RunSetup was given. */
	std::unique_ptr<Screen> CreateSetupScreen(void* engineOptions);
	/** Set by the setup screen's Restart / Quit commands; RunSetup stops its frame loop on them. */
	bool SetupRestartRequested();
	bool SetupQuitRequested();
	/** A static design mock (assets/ui/mocks/...) over the screen @a self (MockScreen.cc). */
	std::unique_ptr<Screen> CreateMockScreen(std::string const& path, ScreenID self);

	/** Sets the "compact" class on element @a rootId when the layout is narrower than @a widthDp (big UI scales,
	 * small windows); screens call it on entry and in Resized. */
	void SetCompact(Rml::ElementDocument*, char const* rootId, float widthDp = 1500);

	/** Feeds one keyboard event of the input queue to the documents (RmlUi's own navigation: Tab, arrows,
	 * Enter/Space on the focused element). Returns true if a document used it. */
	bool ProcessKey(InputAtom const&);

	/** The native tactical HUD (TacticalHud.cc): shown or hidden and refreshed once a frame (BeginFrame). */
	void TacticalHudUpdate();

	/** The mouse is over a part of the HUD that takes clicks (an element with class "hit"). */
	bool TacticalHudWantsMouse();
}
