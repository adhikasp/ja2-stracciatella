// ui_mode: which UI (legacy or native) each screen and overlay uses. No RmlUi here: works without the runtime.
#include "NativeUI.h"

#include "Logger.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>

namespace NativeUI
{

namespace
{
	std::map<std::string, UiMode> g_config;
	std::map<std::string, UiMode> g_override;
}

char const* ToString(UiMode const m)
{
	return m == UiMode::Native ? "native" : "legacy";
}

std::optional<UiMode> ParseUiMode(std::string const& s)
{
	if (s == "native") return UiMode::Native;
	if (s == "legacy") return UiMode::Legacy;
	return std::nullopt;
}

std::vector<ModeKey> const& ModeKeys()
{
	// Screens go native by default when their phase ships (credits: Phase 2; the front end: Phase 3; the laptop: Phase 6); the shared
	// overlays stay legacy on legacy screens until the screens around them are native.
	static std::vector<ModeKey> const keys = {
		{ "setup",      "the pre-game setup screen (game directory, mods, logs)", UiMode::Native },
		{ "credits",    "the credits screen",                           UiMode::Native },
		{ "mainmenu",   "the main menu",                                UiMode::Native },
		{ "options",    "the options screen (with the video settings)", UiMode::Native },
		{ "saveload",   "the save and load screen",                     UiMode::Native },
		{ "newgame",    "the new game settings (GIO) screen",           UiMode::Native },
		{ "loadscreen", "the loading screen",                           UiMode::Native },
		{ "mapscreen",  "the strategic map screen",                     UiMode::Native },
		{ "autoresolve","the auto-resolve battle panel",                UiMode::Native },
		{ "shopkeeper", "the arms-dealer trade screen",                 UiMode::Native },
		{ "tactical",   "the tactical HUD (squad bar, inventory, item description, message log)", UiMode::Native },
		{ "laptop",     "the laptop (e-mail, web sites, finances, ...)", UiMode::Native },
		{ "msgbox",  "message boxes (DoMessageBox and its wrappers)",    UiMode::Legacy },
		{ "tooltip", "fast help of legacy screens",                      UiMode::Legacy },
		{ "toasts",  "screen messages shown as toasts",                  UiMode::Legacy },
		{ "cursor",  "the normal mouse pointer on legacy screens",       UiMode::Legacy },
	};
	return keys;
}

bool IsModeKey(std::string const& key)
{
	auto const& k = ModeKeys();
	return std::any_of(k.begin(), k.end(), [&](ModeKey const& m) { return key == m.key; });
}

void Configure(std::string const& modePairs, float const nativeUiScale)
{
	g_config.clear();
	std::istringstream in(modePairs);
	std::string line;
	while (std::getline(in, line))
	{
		size_t const eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string const key = line.substr(0, eq);
		auto const mode = ParseUiMode(line.substr(eq + 1));
		if (!mode)
		{
			SLOGW("ja2.json ui_mode: \"{}\" is not legacy or native (for {})", line.substr(eq + 1), key);
			continue;
		}
		if (!IsModeKey(key)) SLOGW("ja2.json ui_mode: unknown screen \"{}\"", key);
		g_config[key] = *mode;
	}
	SetUserScale(nativeUiScale);
}

namespace { bool g_reducedMotion = false; }
bool ReducedMotion() { return g_reducedMotion; }
void SetReducedMotion(bool const on) { g_reducedMotion = on; }

void SetModeOverride(std::string const& key, std::optional<UiMode> const mode)
{
	if (!IsModeKey(key)) throw std::invalid_argument("unknown ui_mode key \"" + key + "\"");
	if (mode) g_override[key] = *mode;
	else g_override.erase(key);
}

UiMode ConfiguredMode(std::string const& key)
{
	if (auto it = g_override.find(key); it != g_override.end()) return it->second;
	if (auto it = g_config.find(key); it != g_config.end()) return it->second;
	for (ModeKey const& m : ModeKeys())
	{
		if (key == m.key) return m.defaultMode;
	}
	return UiMode::Legacy;
}

UiMode ResolveMode(std::string const& key, std::string* reason)
{
	if (ConfiguredMode(key) == UiMode::Legacy)
	{
		if (reason) *reason = "configured legacy";
		return UiMode::Legacy;
	}
	std::string why;
	if (!Available(&why))
	{
		if (reason) *reason = why;
		return UiMode::Legacy;
	}
	if (reason) *reason = "configured native";
	return UiMode::Native;
}

}
