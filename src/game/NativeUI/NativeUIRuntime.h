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
	/** A point of the game's canvas (SCREEN_WIDTH x SCREEN_HEIGHT, the legacy UI's pixels: what WorldToUi gives) in
	 * output pixels, where the native UI draws. The window scales and letterboxes the canvas, so this is not the UI
	 * scale. */
	Rml::Vector2f CanvasToOutput(float x, float y);
	/** A world pixel (what the world renderer draws in: the coordinates of GridNoToWorldPixels) in output pixels,
	 * through the world camera. Everything anchored to the world is placed with this. */
	Rml::Vector2f WorldToOutput(float x, float y);
	/** Output pixels per canvas pixel (a length on the canvas, in output pixels). */
	float CanvasScale();
	/** The item picture the native pointer carries (an item held by the mouse), or none with an empty @a src;
	 * @a w x @a h in output pixels. */
	void SetCursorItem(std::string const& src, int w, int h);
	/** The shape of the native pointer: an icon of the design system ("walk", "target"; empty: the plain arrow),
	 * its tone class ("ok", "warn", "no", "foe") and whether a ring surrounds it (an attack). */
	void SetCursorShape(std::string const& icon, std::string const& tone, bool ring);
	/** Width in pixels of the item picture the pointer carries (0 none): what rides to the right of the arrow.
	 * Things placed beside the pointer keep clear of it. */
	int CursorItemWidth();

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
		/** Whether the native mouse pointer is drawn over the screen. The intro/ending cinematic says no: it is
		 * pure picture until the player moves the mouse (docs/ui/intro.md §6). */
		virtual bool ShowsCursor() const { return true; }
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
	/** The intro/ending cinematic (IntroNative.cc): the Smacker scene chain under a fading hint bar. */
	std::unique_ptr<Screen> CreateIntroScreen();
	/** The victory epilogue (EpilogueNative.cc): the campaign's last page before the credits. */
	std::unique_ptr<Screen> CreateEpilogueScreen();
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

	/** The weapon readout overlay (WeaponReadout.cc): refreshed once a frame while it is open (BeginFrame). */
	void WeaponReadoutUpdate();

	/** The native loadout screen (LoadoutScreen.cc): refreshed once a frame while it is open (BeginFrame). */
	void LoadoutUpdate();

	/** The native overhead map and placement (TacticalOverhead.cc): shown, hidden and refreshed once a frame
	 * (BeginFrame). */
	void TacticalOverheadUpdate();
	bool TacticalOverheadActive();
	/** The sector card's minimap, refreshed once a frame with the HUD document. */
	void TacticalMinimapUpdate(Rml::ElementDocument* hud);

	/** The mouse is over a part of the HUD that takes clicks (an element with class "hit"). */
	bool TacticalHudWantsMouse();
	/** The tactical cursor layer (TacticalCursor.cc): registers its element once, refreshed once a frame with the HUD
	 * document (the marker, path and chip over the world, the pointer's shape). */
	void RegisterTacticalCursor();
	void TacticalCursorUpdate(Rml::ElementDocument* hud);
	/** The world overlays (TacticalOverlays.cc): locators, burst impacts, arrows, the rubber band, the item lists
	 * and the pause banner, refreshed once a frame with the HUD document. */
	void RegisterTacticalOverlays();
	void TacticalOverlaysUpdate(Rml::ElementDocument* hud);
	/** A chip the HUD asks for next to the pointer where the world's cursor is not shown (a held item over a pocket or
	 * a squad card: what letting go would do). Set every frame by TacticalHud.cc; read by TacticalCursorUpdate. */
	struct HudChip
	{
		bool shown = false;
		std::string head, why, tone; // tone: "ok" | "warn" | "no"
		std::vector<std::pair<std::string, std::string>> lines; // key, value
	};
	void SetHudChip(HudChip chip);
	/** The native pointer is the tactical cursor: over the HUD, and wherever an item is held by the mouse. */
	bool TacticalHudOwnsCursor();
}
