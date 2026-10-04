#include "AutomationLua.h"
#include "Automation.h"
#include "AutomationSession.h"
#include "BattleScenario.h"
#include "CampaignScenario.h"

#include "Assignments.h"
#include "Font_Control.h"
#include "Game_Clock.h"
#include "Input.h"
#include "Isometric_Utils.h"
#include "UILayout.h"
#include "VideoOptionsScreen.h"
#include "GameLoop.h"
#include "JAScreens.h"
#include "Soldier_Find.h"
#include "WorldDef.h"
#include "Laptop.h"
#include "LaptopSave.h"
#include "Logger.h"
#include "MessageBoxScreen.h"
#include "Overhead.h"
#include "Soldier_Control.h"
#include "Soldier_Create.h"
#include "Soldier_Add.h"
#include "Soldier_Tile.h"
#include "ShopKeeper_Interface.h"
#include "Arms_Dealer.h"
#include "ContentManager.h"
#include "DealerModel.h"
#include "GameInstance.h"
#include "Soldier_Profile.h"
#include "PeopleContent.h"
#include "Quests.h"
#include "Dialogue_Control.h"
#include "Message.h"
#include "Strategic_Exit_GUI.h"
#include "Strategic_Pathing.h"
#include "Strategic_Merc_Handler.h"
#include "Strategic.h"
#include "Map_Screen_Helicopter.h"
#include "Map_Screen_Interface.h"
#include "Map_Screen_Interface_Border.h"
#include "Map_Screen_Interface_Map.h"
#include "HelpScreen.h"
#include "MapScreen.h"
#include "Merc_Hiring.h"
#include "Game_Clock.h"
#include "Strategic_Movement.h"
#include "PreBattle_Interface.h"
#include "Auto_Resolve.h"
#include "Animated_ProgressBar.h"
#include "Loading_Screen.h"
#include "Campaign_Types.h"
#include "Strategic_Movement.h"
#include "Tactical_Placement_GUI.h"
#include "Overhead_Types.h"
#include "StrategicMap.h"
#include "Text.h"
#include "HImage.h"
#include "UiSpikeScreen.h"
#include "WorldSpike.h"
#include "WorldRender.h"
#include "Interactive_Tiles.h"
#include "LOS.h"
#include "OppList.h"
#include "Animation_Control.h"
#include "AI.h"
#include "Lighting.h"
#include "Environment.h"
#include "Rotting_Corpses.h"
#include "Handle_Items.h"
#include "Items.h"
#include "Keys.h"
#include "Interface.h"
#include "Interface_Items.h"
#include "Render_Fun.h"
#include "RenderWorld.h"
#include "Handle_UI.h"
#include "NativeUI.h"
#include "GameViewModelsLua.h"

#include <string_theory/format>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <cstring>
#include <sstream>

namespace Automation
{

namespace
{
	sol::state  g_lua;
	FailureKind g_lastFailure = FailureKind::None;
	int         g_checkFailures = 0;
	int         g_lastMessageBoxResult = 0; // what the last ja2.debug("msgbox") box returned

	constexpr unsigned DEFAULT_TIMEOUT_MS = 10'000;
	constexpr unsigned LONG_TIMEOUT_MS    = 120'000;

	// ja2.recordImageUsage(): screen -> images loaded on it, and the optional JSON-lines log
	std::map<std::string, std::set<std::string>>& ImageUsage()
	{
		static std::map<std::string, std::set<std::string>> usage;
		return usage;
	}
	std::ofstream*& ImageUsageLog()
	{
		static std::ofstream* log = nullptr;
		return log;
	}

	/* Run an API call, remembering what kind of failure (if any) it raised so
	 * that the runner can pick the right exit code. The exception itself is
	 * turned into a Lua error by sol2. */
	template<typename F>
	auto Guarded(F&& f)
	{
		g_lastFailure = FailureKind::None;
		try
		{
			return f();
		}
		catch (TimeoutError const&)      { g_lastFailure = FailureKind::Timeout;     throw; }
		catch (GameCrashedError const&)  { g_lastFailure = FailureKind::Crash;       throw; }
		catch (GameExitedError const&)   { g_lastFailure = FailureKind::Exited;      throw; }
		catch (ExpectationError const&)  { g_lastFailure = FailureKind::Expectation; throw; }
		catch (std::exception const&)    { g_lastFailure = FailureKind::Script;      throw; }
	}

	unsigned Timeout(sol::optional<unsigned> const t, unsigned const def)
	{
		return t ? *t : def;
	}

	SDL_Rect RectFrom(sol::table const& t)
	{
		return { t.get_or("x", 0), t.get_or("y", 0), t.get_or("w", 0), t.get_or("h", 0) };
	}

	/* A locator is either a string (text to look for), or a table with
	 * text/exact/index/within, or x/y coordinates. */
	Locator LocatorFrom(sol::object const& o)
	{
		Locator loc;
		if (o.is<std::string>())
		{
			loc.text = o.as<std::string>();
		}
		else if (o.is<sol::table>())
		{
			sol::table t = o.as<sol::table>();
			loc.text  = t.get_or<std::string>("text", "");
			loc.id    = t.get_or<std::string>("id", "");
			loc.exact = t.get_or("exact", false);
			loc.index = t.get_or("index", 1);
			sol::optional<int> x = t["x"], y = t["y"];
			if (x && y) loc.point = SDL_Point{ *x, *y };
			sol::optional<sol::table> within = t["within"];
			if (within) loc.within = RectFrom(*within);
		}
		else
		{
			throw std::invalid_argument("expected a string or a table locator");
		}
		return loc;
	}

	sol::table ElementTable(Element const& e)
	{
		sol::table t = g_lua.create_table();
		if (e.kind == "native")
		{
			t["id"] = e.nativeId;
			t["role"] = e.name;
			t["focused"] = e.focused;
		}
		else
		{
			t["id"] = e.id;
		}
		t["kind"] = e.kind;
		t["x"] = e.rect.x; t["y"] = e.rect.y; t["w"] = e.rect.w; t["h"] = e.rect.h;
		t["label"] = e.label;
		if (!e.text.empty()) t["text"] = e.text;
		if (!e.name.empty() && e.kind != "native") t["name"] = e.name;
		if (!e.help.empty()) t["help"] = e.help;
		t["enabled"] = e.enabled;
		t["clickable"] = e.clickable;
		return t;
	}

	sol::table TargetTable(Target const& target)
	{
		sol::table t = target.element ? ElementTable(*target.element) : g_lua.create_table();
		t["cx"] = target.point.x;
		t["cy"] = target.point.y;
		t["description"] = target.description;
		return t;
	}

	/* ja2.click("Load Game") / ja2.click(100, 200) / ja2.click{text=..., index=2}
	 * with an optional trailing options table {button=, count=, timeout=}. */
	sol::table PointerAction(sol::variadic_args va, int button, int count, bool press)
	{
		if (va.size() == 0) throw std::invalid_argument("expected a locator or x, y");
		Locator loc;
		size_t next = 1;
		if (va[0].is<int>() && va.size() >= 2 && va[1].is<int>())
		{
			loc.point = SDL_Point{ va[0].as<int>(), va[1].as<int>() };
			next = 2;
		}
		else
		{
			loc = LocatorFrom(va[0]);
		}
		unsigned timeout = DEFAULT_TIMEOUT_MS;
		if (va.size() > next && va[next].is<sol::table>())
		{
			sol::table opts = va[next].as<sol::table>();
			std::string const b = opts.get_or<std::string>("button", "");
			if (b == "right") button = 2;
			else if (b == "middle") button = 3;
			count   = opts.get_or("count", count);
			timeout = opts.get_or("timeout", timeout);
		}
		Target const target = Session::Resolve(loc, timeout);
		SLOGI("[automation] {} {}", press ? "click" : "hover", target.description);
		if (press) Session::Click(target.point.x, target.point.y, button, count);
		else       Session::MouseMove(target.point.x, target.point.y);
		return TargetTable(target);
	}

	std::string HexColor(uint32_t const c)
	{
		char buf[8];
		std::snprintf(buf, sizeof(buf), "#%06x", c & 0xffffff);
		return buf;
	}

	uint32_t ParseColor(std::string const& s)
	{
		if (s.size() == 7 && s[0] == '#') return std::stoul(s.substr(1), nullptr, 16);
		if (s.size() == 4 && s[0] == '#')
		{
			uint32_t const v = std::stoul(s.substr(1), nullptr, 16);
			return ((v >> 8 & 0xf) * 17) << 16 | ((v >> 4 & 0xf) * 17) << 8 | (v & 0xf) * 17;
		}
		throw std::invalid_argument("expected a colour like #rrggbb, got " + s);
	}

	bool ColorNear(uint32_t const a, uint32_t const b, int const tolerance)
	{
		for (int shift = 0; shift <= 16; shift += 8)
		{
			if (std::abs(int(a >> shift & 0xff) - int(b >> shift & 0xff)) > tolerance) return false;
		}
		return true;
	}

	// The tile under screen point (x, y), as the mouse code would see it, or -1.
	int GridUnder(int const x, int const y)
	{
		if (y >= gsVIEWPORT_WINDOW_END_Y) return -1; // the interface panel, not the map
		INT16 wx, wy;
		if (!GetWorldCoordsAtScreenPos(static_cast<INT16>(x), static_cast<INT16>(y), &wx, &wy)) return -1;
		int const grid = MAPROWCOLTOPOS(wy / CELL_Y_SIZE, wx / CELL_X_SIZE);
		return grid == 0xffff ? -1 : grid;
	}

	// Where to click to hit tile @a grid: the centre of the screen area that
	// maps to it. GetGridNoScreenPos() is tuned for drawing sprites and can
	// be a tile off, so search around it with the mouse code's own mapping.
	std::optional<SDL_Point> GridClickPos(int const grid, int const level)
	{
		INT16 sx, sy;
		GetGridNoScreenPos(static_cast<INT16>(grid), static_cast<UINT8>(level), &sx, &sy);
		// that is in world pixels; clicks are in UI pixels (the same without layers)
		LayerPoint const ui = g_ui.worldToUi(sx, sy);
		sx = static_cast<INT16>(ui.x);
		sy = static_cast<INT16>(ui.y);
		int const stepX = g_ui.isLayered() ? 1 : 4;
		int const stepY = g_ui.isLayered() ? 1 : 2;
		long sumX = 0, sumY = 0, n = 0;
		for (int dy = -30; dy <= 30; dy += stepY)
		{
			for (int dx = -60; dx <= 60; dx += stepX)
			{
				if (GridUnder(sx + dx, sy + dy) != grid) continue;
				sumX += sx + dx;
				sumY += sy + dy;
				++n;
			}
		}
		if (n == 0) return std::nullopt;
		return SDL_Point{ int(sumX / n), int(sumY / n) };
	}

	sol::table GameState()
	{
		sol::state& L = g_lua;
		sol::table s = L.create_table();
		s["screen"]     = Session::ScreenName();
		s["frame"]      = Session::Frame();
		s["ms"]         = Session::ElapsedMs();
		s["idle"]       = Session::IsIdle();
		s["messageBox"] = gfInMsgBox;
		if (gfInMsgBox) s["messageBoxText"] = Session::MessageBoxText();

		sol::table time = L.create_table();
		time["day"]    = GetWorldDay();
		time["hour"]   = GetWorldHour();
		time["minute"] = GetWorldMinutesInDay() % 60;
		time["totalMinutes"] = GetWorldTotalMin();
		time["totalSeconds"] = GetWorldTotalSeconds();
		time["compressed"]   = static_cast<bool>(IsTimeBeingCompressed());
		time["paused"] = static_cast<bool>(GamePaused() || gfPauseDueToPlayerGamePause);
		s["time"] = time;

		s["money"]      = LaptopSaveInfo.iCurrentBalance;
		s["laptopMode"] = static_cast<int>(guiCurrentLaptopMode);
		s["sector"]     = gWorldSector.AsShortString().to_std_string();

		sol::table tactical = L.create_table();
		tactical["inCombat"]    = (gTacticalStatus.uiFlags & INCOMBAT) != 0;
		tactical["currentTeam"] = gTacticalStatus.ubCurrentTeam;
		tactical["ourTurn"]     = (gTacticalStatus.uiFlags & INCOMBAT) == 0 || gTacticalStatus.ubCurrentTeam == OUR_TEAM;
		tactical["attackBusy"]  = gTacticalStatus.ubAttackBusyCount; // what waitIdle treats as mid-attack
		tactical["enemyInSector"] = static_cast<bool>(gTacticalStatus.fEnemyInSector);
		// Who is standing in the loaded sector on the other side, so a script can pick a
		// target (the battle e2e track). Dead soldiers stay listed with dead = true.
		sol::table enemies = L.create_table();
		int e = 1;
		FOR_EACH_IN_TEAM(x, ENEMY_TEAM)
		{
			if (!x->bInSector) continue;
			sol::table t = L.create_table();
			t["name"]    = x->name.to_std_string();
			t["class"]   = static_cast<int>(x->ubSoldierClass);
			t["life"]    = static_cast<int>(x->bLife);
			t["lifeMax"] = static_cast<int>(x->bLifeMax);
			t["gridNo"]  = x->sGridNo;
			t["dead"]    = x->bLife <= 0;
			t["level"]     = static_cast<int>(x->bLevel);
			t["direction"] = static_cast<int>(x->bDirection);
			t["stance"]    = static_cast<int>(gAnimControl[x->usAnimState].ubEndHeight);
			t["morale"]    = static_cast<int>(x->bMorale);
			// The AI's own morale verdict (MORALE_HOPELESS..FEARLESS): its reading of the
			// tactical balance, i.e. whether it is close to breaking. 0 = HOPELESS.
			t["aimorale"]  = static_cast<int>(CalcMorale(x));
			t["ap"]        = static_cast<int>(x->bActionPoints);
			// Does the player know about him (what is rendered; may be a last-known position),
			// and can any merc trace an unobstructed line of sight to him within sight range?
			t["known"] = x->bVisible != FALSE;
			bool los = false;
			SOLDIERTYPE* nearest = nullptr;
			INT32 nearestDist = 0;
			FOR_EACH_IN_TEAM(m, OUR_TEAM)
			{
				if (!m->bInSector || m->bLife <= 0 || m->sGridNo == NOWHERE) continue;
				if (SoldierToSoldierLineOfSightTest(m, x, static_cast<UINT8>(MaxDistanceVisible()), TRUE) != 0) los = true;
				INT32 const d = SpacesAway(m->sGridNo, x->sGridNo);
				if (!nearest || d < nearestDist) { nearest = m; nearestDist = d; }
			}
			t["los"] = los;
			// Chance a shot from the nearest merc has to get through (0..100): lower = more cover.
			if (nearest && x->bLife > 0) t["cover"] = static_cast<int>(AISoldierToSoldierChanceToGetThrough(nearest, x));
			if (guiCurrentScreen == GAME_SCREEN && x->sGridNo != NOWHERE)
			{
				if (auto const p = GridClickPos(x->sGridNo, x->bLevel))
				{
					t["screenX"] = p->x;
					t["screenY"] = p->y;
				}
			}
			enemies[e++] = t;
		}
		tactical["enemies"] = enemies;
		// Friendlies who are not on the player's team: the townsfolk/NPCs a script spawns
		// (ja2.debug("npcs")) and militia, so a town-entry test can assert who is there.
		sol::table civilians = L.create_table();
		int c = 1;
		CFOR_EACH_IN_TEAM(x, CIV_TEAM)
		{
			if (!x->bInSector) continue;
			sol::table t = L.create_table();
			t["name"]    = x->name.to_std_string();
			t["profile"] = static_cast<int>(x->ubProfile);
			t["life"]    = static_cast<int>(x->bLife);
			t["gridNo"]  = x->sGridNo;
			t["dead"]    = x->bLife <= 0;
			civilians[c++] = t;
		}
		tactical["civilians"] = civilians;
		s["tactical"] = tactical;

		sol::table mercs = L.create_table();
		int i = 1;
		CFOR_EACH_IN_TEAM(m, OUR_TEAM)
		{
			sol::table t = L.create_table();
			t["name"]       = m->name.to_std_string();
			t["profile"]    = static_cast<int>(m->ubProfile);
			t["sector"]     = m->sSector.AsShortString().to_std_string();
			t["assignment"] = static_cast<int>(m->bAssignment);
			if (m->bAssignment >= 0 && m->bAssignment <= ASSIGNMENT_EMPTY)
			{
				t["assignmentName"] = pAssignmentStrings[m->bAssignment].to_std_string();
			}
			// where the merc is going on the strategic map (his plotted route's last sector), and whether he sleeps
			t["destination"] = SGPSector::FromStrategicIndex(GetLastSectorIdInCharactersPath(m)).AsShortString().to_std_string();
			t["asleep"]   = m->fMercAsleep != 0;
			t["trainStat"] = static_cast<int>(m->bTrainStat);
			t["life"]     = static_cast<int>(m->bLife);
			t["lifeMax"]  = static_cast<int>(m->bLifeMax);
			t["inSector"] = m->bInSector != 0;
			t["gridNo"]   = m->sGridNo;
			t["finalDestination"] = m->sFinalDestination;
			t["level"]     = static_cast<int>(m->bLevel);
			t["direction"] = static_cast<int>(m->bDirection);
			t["stance"]    = static_cast<int>(gAnimControl[m->usAnimState].ubEndHeight);
			t["morale"]    = static_cast<int>(m->bMorale);
			t["ap"]        = static_cast<int>(m->bActionPoints);
			t["maxAp"]     = static_cast<int>(m->bInitialActionPoints);
			if (guiCurrentScreen == GAME_SCREEN && m->bInSector && m->sGridNo != NOWHERE)
			{
				// Where to click on this merc (the tile they stand on).
				if (auto const p = GridClickPos(m->sGridNo, m->bLevel))
				{
					t["screenX"] = p->x;
					t["screenY"] = p->y;
				}
			}
			mercs[i++] = t;
		}
		s["mercs"] = mercs;
		return s;
	}

	// The compiled people & quest registry (PeopleContent.h), for `ja2.game.npcs()`
	// and `ja2.game.quests()`. Definitions come from the registry; quests also carry
	// the live status so an e2e script can assert a transition.
	sol::table NpcListTable()
	{
		sol::table list = g_lua.create_table();
		int i = 1;
		for (People::NpcDef const& n : People::NpcDefs())
		{
			sol::table t = g_lua.create_table();
			t["id"]           = static_cast<int>(n.id);
			t["name"]         = n.name;
			t["kind"]         = People::NpcKindName(n.kind);
			t["homeSectors"]  = n.homeSectors;
			t["placedAtStart"] = n.placedAtStart;
			sol::table quests = g_lua.create_table();
			int q = 1;
			for (People::QuestLink const& l : n.quests)
			{
				sol::table lk = g_lua.create_table();
				People::QuestDef const* const qd = People::FindQuest(l.quest);
				lk["id"]   = static_cast<int>(l.quest);
				lk["name"] = qd ? qd->name : "";
				lk["role"] = People::QuestRoleName(l.role);
				quests[q++] = lk;
			}
			t["quests"] = quests;
			list[i++] = t;
		}
		return list;
	}

	const char* QuestStatusName(UINT8 status)
	{
		switch (status)
		{
			case QUESTNOTSTARTED: return "NOT_STARTED";
			case QUESTINPROGRESS: return "IN_PROGRESS";
			case QUESTDONE:       return "DONE";
		}
		return "UNKNOWN";
	}

	sol::table QuestListTable()
	{
		sol::table list = g_lua.create_table();
		int i = 1;
		for (People::QuestDef const& q : People::QuestDefs())
		{
			sol::table t = g_lua.create_table();
			t["id"]            = static_cast<int>(q.id);
			t["name"]          = q.name;
			t["title"]         = q.title;
			t["status"]        = QuestStatusName(q.id < MAX_QUESTS ? gubQuest[q.id] : QUESTNOTSTARTED);
			t["selfResolving"] = q.selfResolving;
			t["reward"]        = q.reward;
			t["reputationHook"] = q.reputationHook;
			t["deedHook"]      = q.deedHook;
			t["note"]          = q.note;
			auto profiles = [&](std::vector<ProfileID> const& ids, char const* key) {
				sol::table names = g_lua.create_table();
				int k = 1;
				for (ProfileID id : ids) names[k++] = People::NpcName(id);
				t[key] = names;
			};
			profiles(q.givers, "givers");
			profiles(q.resolvers, "resolvers");
			profiles(q.dialogue, "dialogue");
			sol::table prereqs = g_lua.create_table();
			int p = 1;
			for (Quests id : q.prerequisites) prereqs[p++] = People::QuestTitle(id);
			t["prerequisites"] = prereqs;
			list[i++] = t;
		}
		return list;
	}

	sol::table GameTable()
	{
		sol::table game = g_lua.create_table();
		game.set_function("npcs",   [] { return Guarded([] { return NpcListTable(); }); });
		game.set_function("quests", [] { return Guarded([] { return QuestListTable(); }); });
		return game;
	}

	// Turn the enemies standing in the loaded sector into a strategic encounter, as if we had walked into them.
	void FakeEncounter()
	{
		if (!gWorldSector.IsValid()) throw std::runtime_error("no sector is loaded");
		SECTORINFO& si = SectorInfo[gWorldSector.AsByte()];
		si.ubNumAdmins = si.ubNumTroops = si.ubNumElites = 0;
		FOR_EACH_IN_TEAM(e, ENEMY_TEAM)
		{
			if (!e->bInSector || e->bLife == 0) continue;
			if      (e->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR) ++si.ubNumAdmins;
			else if (e->ubSoldierClass == SOLDIER_CLASS_ELITE)         ++si.ubNumElites;
			else                                                       ++si.ubNumTroops;
		}
		gubPBSector = gWorldSector;
		gubEnemyEncounterCode = ENEMY_ENCOUNTER_CODE;
	}

	void RegisterApi(sol::table ja2)
	{
		sol::state& L = g_lua;
		Options const& opt = GetOptions();

		sol::table args = L.create_table();
		for (size_t i = 0; i < opt.scriptArgs.size(); ++i) args[i + 1] = opt.scriptArgs[i];
		ja2["args"]     = args;
		ja2["headless"] = opt.Headless();
		ja2["game"]     = GameTable();

		// --- time ---
		ja2.set_function("step", [](sol::optional<unsigned> frames) {
			return Guarded([&] { Session::Step(frames.value_or(1)); return Session::Frame(); });
		});
		ja2.set_function("wait", [](unsigned ms) { Guarded([&] { Session::Wait(ms); }); });
		ja2.set_function("frame", [] { return Session::Frame(); });
		ja2.set_function("time", [] { return Session::ElapsedMs(); });
		ja2.set_function("waitUntil", [](sol::protected_function pred, sol::optional<unsigned> timeout, sol::optional<std::string> what) {
			Guarded([&] {
				Session::WaitUntil([&] {
					sol::protected_function_result r = pred();
					if (!r.valid())
					{
						sol::error err = r;
						throw std::runtime_error(err.what());
					}
					return r.get<sol::object>().is<bool>() ? r.get<bool>() : r.get<sol::object>() != sol::lua_nil;
				}, Timeout(timeout, DEFAULT_TIMEOUT_MS), what.value_or("condition"));
			});
		});
		ja2.set_function("waitIdle", [](sol::optional<unsigned> timeout) {
			Guarded([&] { Session::WaitIdle(Timeout(timeout, LONG_TIMEOUT_MS)); });
		});
		ja2.set_function("waitScreen", [](std::string const& screen, sol::optional<unsigned> timeout) {
			Guarded([&] {
				Session::WaitUntil([&] { return Session::ScreenName() == screen && Session::IsIdle(); },
					Timeout(timeout, LONG_TIMEOUT_MS), "screen " + screen);
			});
		});
		ja2.set_function("waitFor", [](sol::object loc, sol::optional<unsigned> timeout) {
			return Guarded([&] { return TargetTable(Session::Resolve(LocatorFrom(loc), Timeout(timeout, DEFAULT_TIMEOUT_MS))); });
		});
		ja2.set_function("waitGone", [](sol::object loc, sol::optional<unsigned> timeout) {
			Guarded([&] {
				Locator const l = LocatorFrom(loc);
				Session::WaitUntil([&] { return !Session::Find(l); }, Timeout(timeout, DEFAULT_TIMEOUT_MS), l.Describe() + " to disappear");
			});
		});
		ja2.set_function("waitPixel", [](int x, int y, std::string const& color, sol::optional<int> tolerance, sol::optional<unsigned> timeout) {
			Guarded([&] {
				uint32_t const want = ParseColor(color);
				Session::WaitUntil([&] { return ColorNear(Session::Pixel(x, y), want, tolerance.value_or(8)); },
					Timeout(timeout, LONG_TIMEOUT_MS), ST::format("pixel ({}, {}) to be {}", x, y, color).to_std_string());
			});
		});
		ja2.set_function("waitStable", [](int x, int y, sol::optional<unsigned> timeout) {
			Guarded([&] {
				uint32_t last = Session::Pixel(x, y);
				int same = 0;
				Session::WaitUntil([&] {
					uint32_t const now = Session::Pixel(x, y);
					same = now == last ? same + 1 : 0;
					last = now;
					return same >= 3;
				}, Timeout(timeout, DEFAULT_TIMEOUT_MS), ST::format("pixel ({}, {}) to stop changing", x, y).to_std_string());
			});
		});

		// --- input ---
		ja2.set_function("click", [](sol::variadic_args va) { return Guarded([&] { return PointerAction(va, 1, 1, true); }); });
		ja2.set_function("rclick", [](sol::variadic_args va) { return Guarded([&] { return PointerAction(va, 2, 1, true); }); });
		ja2.set_function("dblclick", [](sol::variadic_args va) { return Guarded([&] { return PointerAction(va, 1, 2, true); }); });
		ja2.set_function("hover", [](sol::variadic_args va) { return Guarded([&] { return PointerAction(va, 1, 1, false); }); });
		ja2.set_function("move", [](int x, int y) { Guarded([&] { Session::MouseMove(x, y); }); });
		ja2.set_function("mouse", [] { auto const p = Session::MousePos(); return std::make_tuple(p.x, p.y); });
		ja2.set_function("mousedown", [](sol::optional<std::string> b) {
			Guarded([&] { Session::MouseButton(b.value_or("left") == "right" ? 2 : 1, true); });
		});
		ja2.set_function("mouseup", [](sol::optional<std::string> b) {
			Guarded([&] { Session::MouseButton(b.value_or("left") == "right" ? 2 : 1, false); });
		});
		ja2.set_function("drag", [](int x0, int y0, int x1, int y1) { Guarded([&] { Session::Drag(x0, y0, x1, y1); }); });
		ja2.set_function("wheel", [](int dy, sol::optional<int> x, sol::optional<int> y) {
			Guarded([&] {
				auto const p = Session::MousePos();
				Session::Wheel(x.value_or(p.x), y.value_or(p.y), dy);
			});
		});
		ja2.set_function("key", [](std::string const& combo, sol::optional<int> times) {
			Guarded([&] { for (int i = 0; i < times.value_or(1); ++i) Session::Key(combo); });
		});
		ja2.set_function("keydown", [](std::string const& k) { Guarded([&] { Session::KeyDown(k); }); });
		ja2.set_function("keyup", [](std::string const& k) { Guarded([&] { Session::KeyUp(k); }); });
		ja2.set_function("type", [](std::string const& text) { Guarded([&] { Session::Type(text); }); });

		// --- looking ---
		ja2.set_function("screen", [] { return Session::ScreenName(); });
		ja2.set_function("idle", [] { return Session::IsIdle(); });
		ja2.set_function("find", [](sol::object loc) -> sol::object {
			return Guarded([&]() -> sol::object {
				auto const t = Session::Find(LocatorFrom(loc));
				if (!t) return sol::lua_nil;
				return TargetTable(*t);
			});
		});
		ja2.set_function("exists", [](sol::object loc) {
			return Guarded([&] { return Session::Find(LocatorFrom(loc)).has_value(); });
		});
		ja2.set_function("ui", [](sol::optional<sol::table> opts) {
			return Guarded([&] {
				bool const all = opts && opts->get_or("all", false);
				sol::table list = g_lua.create_table();
				int i = 1;
				for (Element const& e : Session::Elements())
				{
					// By default: labelled things a user could click, plus
					// greyed-out buttons (worth knowing they exist).
					bool const shown = e.clickable || (e.kind == "button" && !e.enabled);
					if (!all && (!shown || e.label.empty())) continue;
					list[i++] = ElementTable(e);
				}
				return list;
			});
		});
		ja2.set_function("texts", [] {
			return Guarded([&] {
				sol::table list = g_lua.create_table();
				int i = 1;
				for (auto const& t : Session::Texts())
				{
					sol::table e = g_lua.create_table();
					e["text"] = t.text.to_std_string();
					e["x"] = t.rect.x; e["y"] = t.rect.y; e["w"] = t.rect.w; e["h"] = t.rect.h;
					list[i++] = e;
				}
				return list;
			});
		});
		ja2.set_function("pixel", [](int x, int y) { return Guarded([&] { return HexColor(Session::Pixel(x, y)); }); });
		ja2.set_function("pixelIs", [](int x, int y, std::string const& color, sol::optional<int> tolerance) {
			return Guarded([&] { return ColorNear(Session::Pixel(x, y), ParseColor(color), tolerance.value_or(8)); });
		});
		ja2.set_function("screenshot", [](std::string const& path) {
			return Guarded([&] {
				std::string const out = Session::ResolveOutputPath(path);
				Session::Screenshot(out);
				SLOGI("[automation] screenshot {}", out);
				return out;
			});
		});
		ja2.set_function("state", [] { return Guarded([] { return GameState(); }); });
		// ja2.campaign(): the strategic state the campaign harness authors: clock, money,
		// difficulty, towns (ownership/loyalty/militia), sector garrisons, the roster with
		// gear, and quest/fact progress. See ja2.debug("campaign", spec).
		ja2.set_function("campaign", [] { return Guarded([] { return CampaignState(g_lua); }); });
		// {w, h, stdX, stdY}: the screen size and where the classic 640x480 area starts in it.
		ja2.set_function("screenSize", [] {
			sol::table t = g_lua.create_table();
			t["w"] = SCREEN_WIDTH; t["h"] = SCREEN_HEIGHT;
			t["stdX"] = STD_SCREEN_X; t["stdY"] = STD_SCREEN_Y;
			return t;
		});

		// Where the strategic map shows sector (x, y), 1..16 (A..P = 1..16): {x, y, w, h, cx, cy} in screen
		// pixels, for the current layout (the map is scaled on big screens, see MapScreenGeometry).
		ja2.set_function("mapSector", [](int x, int y) {
			SGPBox const b = g_ui.m_map.sectorBox(x, y);
			sol::table t = g_lua.create_table();
			t["x"] = b.x; t["y"] = b.y; t["w"] = b.w; t["h"] = b.h;
			t["cx"] = b.x + b.w / 2; t["cy"] = b.y + b.h / 2;
			return t;
		});

		// Changes the video settings while the game runs, like the Video options do; the current screen is built again
		// for the new layout. Fields (all optional, the rest stays): res = "1280x720" (the window, "auto" = the desktop),
		// uiscale = 0 (auto) .. 4, worldzoom = 0 (same as the UI) .. 4, window = "windowed" | "borderless" | "fullscreen",
		// filter = "linear" | "sharp" | "pixel". Headless sessions have no window: res is the canvas, and the UI scale only
		// applies when the world is a layer of its own (worldzoom differs from uiscale). Returns the screenSize() table
		// plus uiScale, worldZoom and layered. Nothing is written to ja2.json.
		ja2.set_function("setVideo", [](sol::table t) {
			return Guarded([&] {
				auto want = VideoGetDisplaySettings();
				auto quality = VideoGetScaleQuality();
				if (sol::optional<std::string> res = t["res"])
				{
					int w = 0, h = 0;
					if (*res == "auto") want.resX = want.resY = 0;
					else if (std::sscanf(res->c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { want.resX = w; want.resY = h; }
					else throw std::runtime_error("ja2.setVideo: res must be \"WIDTHxHEIGHT\" or \"auto\"");
				}
				if (sol::optional<int> v = t["uiscale"]) want.uiScale = *v;
				if (sol::optional<int> v = t["worldzoom"]) { want.worldZoom = *v; want.worldZoomQ = 0; }
				if (sol::optional<double> z = t["zoom"])
				{
					// a fractional world zoom (Phase 8), in steps of 1/WORLD_ZOOM_STEPS
					want.worldZoomQ = int(*z * VideoLayout::WORLD_ZOOM_STEPS + 0.5);
					want.worldZoom = std::max(1, want.worldZoomQ / VideoLayout::WORLD_ZOOM_STEPS);
				}
				if (sol::optional<std::string> m = t["window"])
				{
					if      (*m == "windowed")   want.windowMode = WindowMode::Windowed;
					else if (*m == "borderless") want.windowMode = WindowMode::BorderlessDesktop;
					else if (*m == "fullscreen") want.windowMode = WindowMode::Fullscreen;
					else throw std::runtime_error("ja2.setVideo: window must be windowed, borderless or fullscreen");
				}
				if (sol::optional<std::string> f = t["filter"])
				{
					if      (*f == "linear") quality = VideoScaleQuality::LINEAR;
					else if (*f == "sharp")  quality = VideoScaleQuality::NEAR_PERFECT;
					else if (*f == "pixel")  quality = VideoScaleQuality::PERFECT;
					else throw std::runtime_error("ja2.setVideo: filter must be linear, sharp or pixel");
				}
				while (guiPendingScreen != NO_PENDING_SCREEN) Session::Step(1); // let a screen change finish
				ST::string error;
				if (!ChangeVideoSettings(want, quality, false, &error))
				{
					throw std::runtime_error(("ja2.setVideo: " + error).to_std_string());
				}
				Session::Step(3); // the screen builds itself again
				sol::table r = g_lua.create_table();
				r["w"] = SCREEN_WIDTH; r["h"] = SCREEN_HEIGHT;
				r["stdX"] = STD_SCREEN_X; r["stdY"] = STD_SCREEN_Y;
				r["uiScale"] = int(g_ui.m_uiScale); r["worldZoom"] = int(g_ui.m_worldZoom);
				r["zoom"] = double(g_ui.m_worldZoomQ) / VideoLayout::WORLD_ZOOM_STEPS;
				r["layered"] = VideoIsLayered();
				return r;
			});
		});

		// --- tactical map ---
		ja2.set_function("gridPos", [](int grid, sol::optional<int> level) {
			return Guarded([&] {
				if (grid < 0 || grid >= WORLD_MAX) throw std::out_of_range("no such grid number");
				auto const p = GridClickPos(grid, level.value_or(0));
				if (!p) throw std::runtime_error(ST::format("tile {} is not on screen", grid).to_std_string());
				return std::make_tuple(p->x, p->y);
			});
		});
		ja2.set_function("gridAt", [](int x, int y) {
			return Guarded([&] {
				return GridUnder(x, y);
			});
		});
		// ja2.los(fromGrid, toGrid, [fromLevel], [toLevel]): is there an unobstructed line of
		// sight between two tiles? A wall, a closed door or a building blocks it. Independent
		// of distance and of who is looking; the battle scenarios use it to set up cover.
		ja2.set_function("los", [](int fromGrid, int toGrid, sol::optional<int> fromLevel, sol::optional<int> toLevel) {
			return Guarded([&] {
				if (fromGrid < 0 || fromGrid >= WORLD_MAX) throw std::out_of_range("ja2.los: bad from grid");
				if (toGrid < 0 || toGrid >= WORLD_MAX) throw std::out_of_range("ja2.los: bad to grid");
				return LocationToLocationLineOfSightTest(
					static_cast<INT16>(fromGrid), static_cast<INT8>(fromLevel.value_or(0)),
					static_cast<INT16>(toGrid), static_cast<INT8>(toLevel.value_or(0)), 255, TRUE) != 0;
			});
		});

		// ja2.debug(what, [a]): open a piece of tactical UI directly, for layout tests that cannot
		// easily reach it through play. what = "exitmenu" (a = direction), "placement", "quote"
		// (a = quote number, spoken by the selected merc), "message" (a = text), "msgbox" (a = text),
		// "loadscreen" (a = id), "prebattle" and "autoresolve" (fake a fight in the current sector),
		// "doormenu" (a = the door's grid number, default: the nearest door), "pickupmenu" (a small
		// pile of tools at the merc's feet and the pick-up menu on it).
		ja2.set_function("debug", [](std::string const& what, sol::optional<sol::object> a, sol::optional<sol::object> b) {
			Guarded([&] {
				if (what == "exitmenu")
				{
					InitSectorExitMenu(a && a->is<int>() ? a->as<int>() : NORTH, 0);
				}
				else if (what == "placement")
				{
					// The GUI reads the battle group's sector: fake one in the current sector.
					static GROUP dummy;
					dummy.ubSector = gWorldSector;
					gpBattleGroup = &dummy;
					InitTacticalPlacementGUI();
				}
				else if (what == "quote")
				{
					SOLDIERTYPE const* const s = GetSelectedMan();
					if (!s) throw std::runtime_error("no selected merc");
					TacticalCharacterDialogue(s, a && a->is<int>() ? a->as<int>() : 0);
				}
				else if (what == "message")
				{
					ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_INTERFACE, ST::string(a && a->is<std::string>() ? a->as<std::string>() : "debug message"));
				}
				else if (what == "msgbox")
				{
					// b = "ok" (default), "yesno", "yesnolie", "okskip", "four"; the answer goes to ja2.lastMessageBoxResult()
					std::string const kind = b && b->is<std::string>() ? b->as<std::string>() : "ok";
					MessageBoxFlags const flags = kind == "yesno" ? MSG_BOX_FLAG_YESNO : kind == "yesnolie" ? MSG_BOX_FLAG_YESNOLIE :
						kind == "okskip" ? MSG_BOX_FLAG_OKSKIP : kind == "four" ? MSG_BOX_FLAG_FOUR_NUMBERED_BUTTONS : MSG_BOX_FLAG_OK;
					g_lastMessageBoxResult = 0;
					DoMessageBox(MSG_BOX_BASIC_STYLE, ST::string(a && a->is<std::string>() ? a->as<std::string>() : "Debug message box"),
						guiCurrentScreen, flags, [](MessageBoxReturnValue r) { g_lastMessageBoxResult = int(r); });
				}
				else if (what == "loadscreen")
				{
					// A loading screen with its progress bar half full (a = loading screen id).
					DisplayLoadScreenWithID(a && a->is<int>() ? a->as<int>() : LOADINGSCREEN_DAYGENERIC);
					CreateLoadingScreenProgressBar();
					RenderProgressBar(0, 60);
				}
				else if (what == "prebattle")
				{
					// Fake an enemy encounter in the current sector on the map screen and open the pre-battle panel.
					FakeEncounter();
					InitPreBattleInterface(nullptr, false);
				}
				else if (what == "help")
				{
					// Test aid: open the map screen's help overlay (the first-visit help for the current state).
					ShouldTheHelpScreenComeUp(HelpScreenDetermineWhichMapScreenHelpToShow(), TRUE);
				}
				else if (what == "militia")
				{
					// Test aid: give town a (default 1) some militia and open redistribution on it.
					int const town = a && a->is<int>() ? a->as<int>() : 1;
					for (int s = 0; s < 256; ++s)
					{
						if (GetTownIdForSector(SGPSector(s)) != town) continue;
						SectorInfo[s].ubNumberOfCivsAtLevel[GREEN_MILITIA]   = 5;
						SectorInfo[s].ubNumberOfCivsAtLevel[REGULAR_MILITIA] = 3;
						SectorInfo[s].ubNumberOfCivsAtLevel[ELITE_MILITIA]   = 1;
						StrategicMap[SGPSector(s).AsStrategicIndex()].fEnemyControlled = FALSE;
					}
					fShowMilitia = TRUE;
					sSelectedMilitiaTown = INT16(town);
				}
				else if (what == "clearenemies")
				{
					// Test aid: remove every enemy from the loaded sector (the soldiers and the strategic counts), as if
					// the battle had been won, so that a script can reach the states of a secured sector quickly.
					FOR_EACH_IN_TEAM(s, ENEMY_TEAM) TacticalRemoveSoldier(*s);
					EliminateAllEnemies(gWorldSector);
					gTacticalStatus.fEnemyInSector = FALSE;
				}
				else if (what == "helicopter")
				{
					// Test aid: Skyrider's helicopter as if he had been hired, parked in sector a (default "A9")
					std::string const at = a && a->is<std::string>() ? a->as<std::string>() : "A9";
					SetUpHelicopterForPlayer(SGPSector::FromShortString(at));
					ReBuildCharactersList();
				}
				else if (what == "updatebox")
				{
					// Test aid: the map screen's update box, as if merc a (name) had finished his assignment
					std::string const name = a && a->is<std::string>() ? a->as<std::string>() : "";
					SOLDIERTYPE* found = nullptr;
					FOR_EACH_IN_TEAM(s, OUR_TEAM) if (s->name.to_std_string() == name) found = s;
					if (!found) throw std::runtime_error("no merc " + name);
					AddSoldierToWaitingListQueue(*found);
					AddReasonToWaitingListQueue(ASSIGNMENT_FINISHED_FOR_UPDATE);
					AddDisplayBoxToWaitingQueue();
				}
				else if (what == "doormenu")
				{
					// Test aid: the door menu on the nearest door (a = the door's grid number)
					SOLDIERTYPE* const s = GetSelectedMan();
					if (!s) throw std::runtime_error("no selected merc");
					DOOR* found = nullptr;
					INT32 best = NOWHERE;
					for (DOOR& d : DoorTable)
					{
						if (a && a->is<int>() && d.sGridNo != (INT16)a->as<int>()) continue;
						INT32 const dist = SpacesAway(s->sGridNo, d.sGridNo);
						if (!found || dist < best) { found = &d; best = dist; }
					}
					if (!found) throw std::runtime_error("no door in this sector");
					InitDoorOpenMenu(s, found, FALSE);
				}
				else if (what == "pickupmenu")
				{
					// Test aid: a small pile of tools at the merc's feet and the pick-up menu on it
					SOLDIERTYPE* const s = GetSelectedMan();
					if (!s) throw std::runtime_error("no selected merc");
					for (UINT16 const item : { (UINT16)CROWBAR, (UINT16)LOCKSMITHKIT, (UINT16)SHAPED_CHARGE })
					{
						OBJECTTYPE obj;
						CreateItem(item, 90, &obj);
						AddItemToPool(s->sGridNo, &obj, VISIBLE, s->bLevel, 0, -1);
					}
					ITEM_POOL* const pool = GetItemPool(s->sGridNo, s->bLevel);
					if (!pool) throw std::runtime_error("no items on this tile");
					InitializeItemPickupMenu(s, s->sGridNo, pool, 0);
				}
				else if (what == "killmerc")
				{
					// Test aid: merc a (name) dies (the strategic handling of a death: assignment, list, email)
					std::string const name = a && a->is<std::string>() ? a->as<std::string>() : "";
					SOLDIERTYPE* found = nullptr;
					FOR_EACH_IN_TEAM(s, OUR_TEAM) if (s->name.to_std_string() == name) found = s;
					if (!found) throw std::runtime_error("no merc " + name);
					found->bLife = 0;
					StrategicHandlePlayerTeamMercDeath(*found);
					ReBuildCharactersList();
				}
				else if (what == "hiretransit")
				{
					// Test aid: an A.I.M. merc (a: profile id) hired for a week, arriving in b minutes (default 600)
					MERC_HIRE_STRUCT h{};
					// a: the profile id; without one, the first A.I.M. merc who can be hired now
					int pid = a && a->is<int>() ? a->as<int>() : -1;
					for (int i = 0; pid < 0 && i < 40; ++i)
					{
						if (GetProfile(ProfileID(i)).bMercStatus == 0 && !FindSoldierByProfileID(ProfileID(i))) pid = i;
					}
					if (pid < 0) throw std::runtime_error("no A.I.M. merc to hire");
					h.ubProfileID = UINT8(pid);
					h.sSector = g_merc_arrive_sector;
					h.iTotalContractLength = 7;
					h.fCopyProfileItemsOver = FALSE;
					h.uiTimeTillMercArrives = GetWorldTotalMin() + UINT32(b && b->is<int>() ? b->as<int>() : 600);
					h.fUseLandingZoneForArrival = TRUE;
					h.ubInsertionCode = INSERTION_CODE_ARRIVING_GAME;
					h.bWhatKindOfMerc = MERC_TYPE__AIM_MERC;
					if (HireMerc(h) != MERC_HIRE_OK) throw std::runtime_error("hiring failed");
					ReBuildCharactersList();
				}
				else if (what == "autoresolve")
				{
					// Fake an enemy encounter in the current sector and go straight into auto resolve.
					FakeEncounter();
					EnterAutoResolveMode(gubPBSector);
				}
				else if (what == "shopkeeper")
				{
					// Test aid: open the native/legacy arms-dealer trade screen with dealer a (default Tony). The
					// dealer NPC is spawned next to the selected merc if he is not already in the sector, so a script
					// can reach the screen without travelling.
					int const dealer_id = a && a->is<int>() ? a->as<int>() : ARMS_DEALER_TONY;
					const DealerModel* const dealer = GCM->getDealer(static_cast<ArmsDealerID>(dealer_id));
					if (!dealer) throw std::runtime_error("ja2.debug(\"shopkeeper\"): no such dealer");
					SOLDIERTYPE* const merc = GetSelectedMan();
					if (!merc) throw std::runtime_error("ja2.debug(\"shopkeeper\"): no selected merc");
					SOLDIERTYPE* npc = FindSoldierByProfileID(dealer->profileID);
					if (!npc || !npc->bInSector)
					{
						SOLDIERCREATE_STRUCT cs{};
						cs.bTeam            = CIV_TEAM;
						cs.ubProfile        = dealer->profileID;
						cs.sSector          = gWorldSector;
						cs.sInsertionGridNo = merc->sGridNo;
						npc = TacticalCreateSoldier(cs);
						if (npc) AddSoldierToSector(npc);
					}
					if (npc)
					{
						// stand beside the merc: CanMercInteractWithSelectedShopkeeper needs line of sight at a
						// nonzero range (a shared tile has range 0, which its visibility test rejects).
						INT16 const beside = NewGridNo(merc->sGridNo, DirectionInc(NORTH));
						TeleportSoldier(*npc, beside != NOWHERE ? beside : merc->sGridNo, true);
					}
					EnterShopKeeperInterfaceScreen(dealer->profileID);
				}
				else if (what == "battle")
				{
					// Stage a deterministic fight in the loaded sector (docs/plan/e2e-tactical-battles.md).
					if (!a || !a->is<sol::table>()) throw std::runtime_error("ja2.debug(\"battle\", spec)");
					StageBattle(a->as<sol::table>());
				}
				else if (what == "fire")
				{
					// Order the selected merc to shoot at a tile through the real fire-weapon event.
					if (!a || !a->is<int>()) throw std::runtime_error("ja2.debug(\"fire\", gridNo)");
					FireAtGrid(GetSelectedMan(), static_cast<INT16>(a->as<int>()));
				}
				else if (what == "campaign")
				{
					// Author a whole campaign state on the live globals (docs/plan/e2e-campaign-state.md).
					if (!a || !a->is<sol::table>()) throw std::runtime_error("ja2.debug(\"campaign\", spec)");
					StageCampaign(a->as<sol::table>());
				}
				else if (what == "entersector")
				{
					// Move the team into a sector and load it in tactical, for a world-map step.
					if (!a || !a->is<sol::table>()) throw std::runtime_error("ja2.debug(\"entersector\", spec)");
					EnterSector(a->as<sol::table>());
				}
				else if (what == "npcs")
				{
					// Spawn townsfolk / named NPCs in the loaded sector, near the team.
					SpawnNpcs(a ? *a : sol::object(sol::nil));
				}
				else if (what == "uispike_rml" || what == "uispike_inhouse")
				{
					// Phase 0 UI toolkit spike: the save/load spike screen in RmlUi or the in-house layer
					UiSpikeOpen(what.substr(8));
				}
				else if (what == "styledemo")
				{
					// Phase 1 style direction (B, Night Ops, chosen): ja2.debug("styledemo", "b", "mainmenu" | "squadbar" | "mapscreen")
					std::string const dir = a && a->is<std::string>() ? a->as<std::string>() : "b";
					std::string const screen = b && b->is<std::string>() ? b->as<std::string>() : "mainmenu";
					UiSpikeOpen("style:" + dir + ":" + screen);
				}
				else if (what == "gallery")
				{
					// Phase 1 design-system gallery: ja2.debug("gallery", [page = "controls"], [ui scale = 1])
					// pages: controls, data, overlays, game, icons, tokens; scale: 1, 1.25, 1.5, 2
					std::string const page = a && a->is<std::string>() ? a->as<std::string>() : "controls";
					double const scale = b && b->is<double>() ? b->as<double>() : 1.0;
					UiSpikeOpen("gallery:" + page + ":" + std::to_string(scale));
				}
				else if (what == "light")
				{
					// World scenes for the renderer tests (Phase 8): the ambient light level (a; 3 = day ...
					// NORMAL_LIGHTLEVEL_NIGHT = 12), with the night lights on when b is true
					int const level = a && a->is<int>() ? a->as<int>() : NORMAL_LIGHTLEVEL_NIGHT;
					LightSetBaseLevel(UINT8(level));
					if (b && b->is<bool>() && b->as<bool>()) TurnOnNightLights();
					HandlePlayerTogglingLightEffects(FALSE);
					SetRenderFlags(RENDER_FLAG_FULL);
				}
				else if (what == "item")
				{
					// an item (b = item index, default 1) on the ground at grid a
					if (!a || !a->is<int>()) throw std::runtime_error("ja2.debug(\"item\", grid, [item])");
					OBJECTTYPE o;
					CreateItem(UINT16(b && b->is<int>() ? b->as<int>() : 1), 100, &o);
					AddItemToPool(INT16(a->as<int>()), &o, VISIBLE, 0, 0, -1);
					SetRenderFlags(RENDER_FLAG_FULL);
				}
				else if (what == "corpse")
				{
					// a corpse of the selected merc's body type at grid a (b = direction)
					SOLDIERTYPE const* const s = GetSelectedMan();
					if (!s || !a || !a->is<int>()) throw std::runtime_error("ja2.debug(\"corpse\", grid): needs a selected merc");
					ROTTING_CORPSE_DEFINITION def{};
					def.ubType = SMERC_BCK;
					def.ubBodyType = s->ubBodyType;
					def.sGridNo = INT16(a->as<int>());
					def.HeadPal = s->HeadPal;
					def.VestPal = s->VestPal;
					def.SkinPal = s->SkinPal;
					def.PantsPal = s->PantsPal;
					def.bDirection = INT8(b && b->is<int>() ? b->as<int>() : 3);
					def.uiTimeOfDeath = GetWorldTotalMin();
					def.bVisible = 1;
					ROTTING_CORPSE* const c = AddRottingCorpse(&def);
					if (!c) throw std::runtime_error("ja2.debug(\"corpse\"): could not add it");
					c->def.bVisible = 1;
					SetRenderFlags(RENDER_FLAG_FULL);
				}
				else if (what == "roof")
				{
					// takes the roof off the room at grid a, as when a merc walks in (an interior view)
					if (!a || !a->is<int>()) throw std::runtime_error("ja2.debug(\"roof\", grid)");
					UINT8 const room = GetRoom(UINT16(a->as<int>()));
					if (room == NO_ROOM) throw std::runtime_error("ja2.debug(\"roof\"): no room there");
					RemoveRoomRoof(UINT16(a->as<int>()), room, nullptr);
					SetRenderFlags(RENDER_FLAG_FULL);
				}
				else if (what == "bookmarks")
				{
					// Test aid: every web bookmark set and Bobby Ray's open, as later in a campaign, so that a script
					// can visit every site of the laptop from a new game.
					for (INT32 b = AIM_BOOKMARK; b < CANCEL_STRING; ++b) SetBookMark(b);
					LaptopSaveInfo.fBobbyRSiteCanBeAccessed = TRUE;
				}
				else if (what == "mock")
				{
					// M2 design mock through the native runtime: ja2.debug("mock", "phase3/mainmenu") shows
					// <ui dir>/mocks/phase3/mainmenu.rml over the current screen until Esc
					std::string const name = a && a->is<std::string>() ? a->as<std::string>() : "";
					if (name.empty() || name.find("..") != std::string::npos) throw std::runtime_error("ja2.debug(\"mock\", name): bad name");
					NativeUI::OpenMock("mocks/" + name + ".rml");
				}
				else
				{
					throw std::runtime_error(("ja2.debug: unknown target " + what).c_str());
				}
			});
		});

		// ja2.recordImageUsage([file]): from now on, note every image the game loads with the screen it was loaded
		// on (the asset usage map, tools/assets/usage.py). With a file (relative to -out), also appends one JSON line
		// per load to it. ja2.imageUsage() returns {screen = {file, ...}, ...} recorded so far.
		ja2.set_function("recordImageUsage", [](sol::optional<std::string> file) {
			Guarded([&] {
				static std::ofstream log;
				if (log.is_open()) log.close();
				if (file) log.open(Session::ResolveOutputPath(*file), std::ios::app);
				ImageUsageLog() = file ? &log : nullptr;
				SetImageLoadHook([](ST::string const& f) {
					std::string const screen = Session::ScreenName();
					ImageUsage()[screen].insert(f.to_std_string());
					if (std::ofstream* l = ImageUsageLog())
					{
						*l << "{\"screen\": \"" << screen << "\", \"file\": \"" << f.to_std_string() << "\", \"frame\": "
							<< Session::Frame() << "}\n";
						l->flush();
					}
				});
			});
		});
		ja2.set_function("imageUsage", [] {
			sol::table t = g_lua.create_table();
			for (auto const& [screen, files] : ImageUsage())
			{
				sol::table list = g_lua.create_table();
				int i = 1;
				for (auto const& f : files) list[i++] = f;
				t[screen] = list;
			}
			return t;
		});

		// ja2.worldEquivalence([dir], [{gpu = true}]): the Phase 8 world renderer against the software renderer on the
		// current view (WorldRender.h): the recorded instances on the CPU pipeline and on the GPU (when a device
		// is available), each compared pixel by pixel with a full software redraw. PNGs to dir (relative to -out).
		ja2.set_function("worldEquivalence", [](sol::optional<std::string> dir, sol::optional<sol::table> opts) {
			return Guarded([&] {
				std::string out;
				if (dir && !dir->empty()) { out = Session::ResolveOutputPath(*dir); std::filesystem::create_directories(out); }
				bool const gpu = opts ? opts->get_or("gpu", true) : true;
				WorldEquivalenceResult const r = RunWorldEquivalence(out, gpu);
				sol::table t = g_lua.create_table();
				t["width"] = r.width;
				t["height"] = r.height;
				t["instances"] = r.instances;
				t["sprites"] = r.sprites;
				t["palettes"] = r.palettes;
				sol::table ops = g_lua.create_table();
				for (auto const& [name, n] : r.ops) ops[name] = n;
				t["ops"] = ops;
				t["pixels"] = double(r.pixels);
				t["pipelineDifferent"] = double(r.pipelineDifferent);
				t["pipelinePercent"] = r.pipelinePercent;
				t["gpuRan"] = r.gpuRan;
				t["gpuDriver"] = r.gpuDriver;
				t["gpuError"] = r.gpuError;
				t["gpuDifferent"] = double(r.gpuDifferent);
				t["gpuPercent"] = r.gpuPercent;
				t["gpuVsPipelineDifferent"] = double(r.gpuVsPipelineDifferent);
				t["legacyMs"] = r.legacyMs;
				t["recordMs"] = r.recordMs;
				t["pipelineMs"] = r.pipelineMs;
				t["gpuMs"] = r.gpuMs;
				t["legacyPng"] = r.legacyPng;
				t["pipelinePng"] = r.pipelinePng;
				t["gpuPng"] = r.gpuPng;
				t["pipelineDiffPng"] = r.pipelineDiffPng;
				t["gpuDiffPng"] = r.gpuDiffPng;
				return t;
			});
		});

		// ja2.worldRenderer(): {requested, active, error, instances, recordMs, rasterMs, gpuSubmitMs, gpuWaitMs, ...}
		ja2.set_function("worldRenderer", [] {
			sol::table t = g_lua.create_table();
			t["requested"] = WorldRendererName(WorldRendererRequested());
			t["active"] = WorldRendererName(WorldRendererActive());
			t["error"] = WorldRendererError();
			WorldRenderStats const& s = WorldRenderLastStats();
			t["instances"] = s.instances;
			t["palettes"] = s.palettes;
			t["spritePoolPixels"] = double(s.spritePoolPixels);
			t["recordMs"] = s.recordMs;
			t["rasterMs"] = s.rasterMs;
			t["gpuBinMs"] = s.gpuBinMs;
			t["gpuSubmitMs"] = s.gpuSubmitMs;
			t["gpuWaitMs"] = s.gpuWaitMs;
			t["uploadBytes"] = double(s.uploadBytes);
			t["frameMs"] = s.frameMs;
			t["staticHits"] = s.staticHits;
			t["staticMisses"] = s.staticMisses;
			t["wallMs"] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); // wall clock, for frame-rate measurements
			return t;
		});

		// ja2.realClock(): the game runs on the wall clock from now on (frame-rate measurements; not reproducible)
		// With {readback = false} GPU world frames are no longer copied back for screenshots (as in normal play).
		ja2.set_function("realClock", [](sol::optional<sol::table> o) {
			sgp::Clock::DisableVirtual();
			if (o) WorldRendererSetReadback(o->get_or("readback", true));
			if (o) { if (sol::optional<int> fps = (*o)["fps"]) VideoSetTargetFPS(*fps); } // 0 = unlimited
		});

		// ja2.frameTiming([reset]): wall-clock frame intervals since the last reset {frames, samples, mean, p50, p95, max}
		ja2.set_function("frameTiming", [](sol::optional<bool> reset) {
			WorldFrameTiming const f = WorldRenderTiming(reset.value_or(false));
			sol::table t = g_lua.create_table();
			t["frames"] = double(f.frames); t["samples"] = f.samples;
			t["mean"] = f.meanMs; t["p50"] = f.p50Ms; t["p95"] = f.p95Ms; t["max"] = f.maxMs;
			return t;
		});

		// ja2.setWorldRenderer("software" | "gpu" | "pipeline"): switches now; returns what is active afterwards
		ja2.set_function("setWorldStaticCache", [](bool on) { WorldRendererSetStaticCache(on); });
		ja2.set_function("setWorldRenderer", [](std::string const& name) {
			return Guarded([&] { return std::string(WorldRendererName(WorldRendererSwitch(ParseWorldRenderer(name)))); });
		});

		// ja2.pick(): what the mouse picks in tactical now: {grid (cursor tile), interactive (tile of the interactive
		// structure under the mouse, e.g. a door, or -1), target (soldier id under the mouse or -1)}
		ja2.set_function("pick", [] {
			sol::table t = g_lua.create_table();
			t["grid"] = int(guiCurrentCursorGridNo);
			INT16 g = NOWHERE;
			LEVELNODE const* const n = GetCurInteractiveTileGridNo(&g);
			t["interactive"] = n ? int(g) : -1;
			t["target"] = gUIFullTarget ? int(gUIFullTarget->ubID) : -1;
			return t;
		});

		// ja2.spikeWorld([dir]): the world renderer spike on the current sector (WorldSpike.h). Returns the pixel
		// diff against the legacy renderer and timings; writes world_legacy/world_spike/world_diff.png to dir
		// (relative to -out) when given.
		ja2.set_function("spikeWorld", [](sol::optional<std::string> dir) {
			return Guarded([&] {
				std::string out;
				if (dir) { out = Session::ResolveOutputPath(*dir); std::filesystem::create_directories(out); }
				WorldSpikeResult const r = RunWorldSpike(out);
				sol::table t = g_lua.create_table();
				t["width"] = r.width;
				t["height"] = r.height;
				t["pixels"] = double(r.pixels);
				t["different"] = double(r.different);
				t["percent"] = r.percent;
				t["instances"] = r.instances;
				t["skipped"] = r.skipped;
				t["sprites"] = r.sprites;
				t["legacyMs"] = r.legacyMs;
				t["buildMs"] = r.buildMs;
				t["rasterMs"] = r.rasterMs;
				t["legacyPng"] = r.legacyPng;
				t["spikePng"] = r.spikePng;
				t["diffPng"] = r.diffPng;
				return t;
			});
		});

		// ja2.spike(): the open UI spike screen: {toolkit, lastFrameMs, meanFrameMs, frames, selected, hovered,
		// modal, status, elements = {{id, x, y, w, h}, ...}} (element ids are what Phase 2 automation will target).
		ja2.set_function("spike", [] {
			return Guarded([&] {
				UiSpikeInfo const info = UiSpikeGetInfo();
				sol::table t = g_lua.create_table();
				t["toolkit"]     = info.toolkit;
				t["lastFrameMs"] = info.lastFrameMs;
				t["meanFrameMs"] = info.meanFrameMs;
				t["frames"]      = info.frames;
				t["selected"]    = info.selected;
				t["hovered"]     = info.hovered;
				t["modal"]       = info.modal;
				t["status"]      = info.status;
				sol::table els = g_lua.create_table();
				int i = 1;
				for (auto const& e : info.elements)
				{
					els[i++] = g_lua.create_table_with("id", e.id, "x", e.x, "y", e.y, "w", e.w, "h", e.h);
				}
				t["elements"] = els;
				sol::table problems = g_lua.create_table();
				for (size_t k = 0; k < info.problems.size(); ++k) problems[k + 1] = info.problems[k];
				t["problems"] = problems;
				return t;
			});
		});

		// --- game ---
		ja2.set_function("saves", [] {
			sol::table list = g_lua.create_table();
			int i = 1;
			for (auto const& s : Session::Saves()) list[i++] = s;
			return list;
		});
		ja2.set_function("load", [](std::string const& save, sol::optional<unsigned> timeout) {
			Guarded([&] { Session::Load(save, Timeout(timeout, LONG_TIMEOUT_MS)); });
		});
		ja2.set_function("save", [](std::string const& name, sol::optional<std::string> desc) {
			Guarded([&] { Session::Save(name, desc.value_or(name)); });
		});
		ja2.set_function("quit", [] { Session::Quit(); });

		// --- test helpers ---
		ja2.set_function("log", [](sol::variadic_args va) {
			std::string msg;
			for (auto v : va) msg += (msg.empty() ? "" : " ") + g_lua["tostring"](v).get<std::string>();
			SLOGI("[script] {}", msg);
			std::fprintf(stderr, "%s\n", msg.c_str());
		});
		ja2.set_function("expect", [](sol::object cond, sol::optional<std::string> msg) {
			Guarded([&] {
				if (!cond.valid() || cond == sol::lua_nil || (cond.is<bool>() && !cond.as<bool>()))
				{
					throw ExpectationError("expectation failed: " + msg.value_or("(no message)"));
				}
			});
		});
		// Legacy regions and buttons must lie inside the screen; native documents must pass the layout audit
		// (nothing off screen, cut sideways, overlapping or truncated). ja2.layoutProblems() lists the same.
		ja2.set_function("assertInsideScreen", [] {
			Guarded([&] {
				auto const bad = Session::LayoutProblems();
				if (bad.empty()) return;
				std::string msg = ST::format("{} layout problem(s) on the {}x{} screen:", bad.size(), SCREEN_WIDTH, SCREEN_HEIGHT).to_std_string();
				for (std::string const& p : bad) msg += "\n  " + p;
				throw ExpectationError(msg);
			});
		});
		ja2.set_function("assertLayout", [] {
			Guarded([&] {
				auto const bad = Session::LayoutProblems();
				if (bad.empty()) return;
				std::string msg = ST::format("{} layout problem(s):", bad.size()).to_std_string();
				for (std::string const& p : bad) msg += "\n  " + p;
				throw ExpectationError(msg);
			});
		});
		ja2.set_function("layoutProblems", [] {
			return Guarded([&] {
				sol::table t = g_lua.create_table();
				int i = 1;
				for (std::string const& p : Session::LayoutProblems()) t[i++] = p;
				return t;
			});
		});

		// --- native UI (docs/automation.md, "Native UI") ---
		ja2.set_function("setUiMode", [](std::string const& key, sol::optional<std::string> mode) {
			Guarded([&] {
				std::optional<NativeUI::UiMode> m;
				if (mode && *mode != "default")
				{
					m = NativeUI::ParseUiMode(*mode);
					if (!m) throw std::invalid_argument("ja2.setUiMode: mode must be legacy, native or default");
				}
				NativeUI::SetModeOverride(key, m);
			});
		});
		ja2.set_function("uiMode", [](std::string const& key) {
			return Guarded([&] {
				if (!NativeUI::IsModeKey(key)) throw std::invalid_argument("unknown ui_mode key " + key);
				std::string reason;
				sol::table t = g_lua.create_table();
				t["configured"] = NativeUI::ToString(NativeUI::ConfiguredMode(key));
				t["resolved"] = NativeUI::ToString(NativeUI::ResolveMode(key, &reason));
				t["reason"] = reason;
				return t;
			});
		});
		ja2.set_function("setUiScale", [](double scale) {
			Guarded([&] {
				NativeUI::SetUserScale(float(scale));
				Session::Step(2);
			});
		});
		ja2.set_function("nativeUi", [] {
			return Guarded([&] {
				NativeUI::Info const i = NativeUI::GetInfo();
				sol::table t = g_lua.create_table();
				t["running"] = i.running;
				t["renderer"] = i.renderer;
				t["w"] = i.width; t["h"] = i.height;
				t["dp"] = i.dp;
				t["uiScale"] = i.userScale;
				t["screen"] = i.screen;
				t["warnings"] = i.warnings;
				t["capturesMouse"] = NativeUI::CapturesMouse();
				t["focused"] = NativeUI::FocusedId();
				sol::table docs = g_lua.create_table();
				for (size_t k = 0; k < i.documents.size(); ++k) docs[k + 1] = i.documents[k];
				t["documents"] = docs;
				sol::table modes = g_lua.create_table();
				for (auto const& m : NativeUI::ModeKeys()) modes[m.key] = NativeUI::ToString(NativeUI::ResolveMode(m.key));
				t["modes"] = modes;
				return t;
			});
		});
		ja2.set_function("focus", [](std::string const& id) {
			Guarded([&] {
				if (!NativeUI::Focus(id)) throw std::runtime_error("ja2.focus: no native element " + id);
				Session::Step(1);
			});
		});
		ja2.set_function("lastMessageBoxResult", [] { return g_lastMessageBoxResult; });
		ja2.set_function("toast", [](std::string const& text, sol::optional<std::string> kind) {
			Guarded([&] {
				std::string const k = kind.value_or("info");
				NativeUI::Toast(text, k == "ok" ? NativeUI::ToastKind::Ok : k == "warn" ? NativeUI::ToastKind::Warn :
					k == "danger" ? NativeUI::ToastKind::Danger : NativeUI::ToastKind::Info);
			});
		});
		RegisterViewModelApi(g_lua, ja2);
		ja2.set_function("check", [](sol::object cond, sol::optional<std::string> msg) {
			bool const ok = cond.valid() && cond != sol::lua_nil && !(cond.is<bool>() && !cond.as<bool>());
			if (!ok)
			{
				++g_checkFailures;
				SLOGE("[script] check failed: {}", msg.value_or("(no message)"));
				std::fprintf(stderr, "check failed: %s\n", msg.value_or("(no message)").c_str());
			}
			return ok;
		});
	}

	std::string LuaQuote(std::string const& s)
	{
		std::string out = "\"";
		for (char const c : s)
		{
			switch (c)
			{
				case '"':  out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n";  break;
				case '\r': break;
				default:   out += c;      break;
			}
		}
		return out + "\"";
	}
}


sol::state& Lua() { return g_lua; }
FailureKind LastFailure() { return g_lastFailure; }
int CheckFailures() { return g_checkFailures; }

void InitLua(std::vector<std::string> const& paths)
{
	g_lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::table,
		sol::lib::math, sol::lib::os, sol::lib::io, sol::lib::debug);
	// Turn C++ exceptions into plain Lua errors; the runner reports them.
	g_lua.set_exception_handler([](lua_State* L, sol::optional<std::exception const&>, sol::string_view what) {
		return sol::stack::push(L, what);
	});
	std::string searchPath;
	for (auto const& dir : paths) searchPath += dir + "/?.lua;";
	std::string const defaults = g_lua["package"]["path"];
	g_lua["package"]["path"] = searchPath + defaults;
	RegisterApi(g_lua.create_named_table("ja2"));
}


std::string TranslateLegacyScript(std::string const& source, std::string& error)
{
	std::ostringstream lua;
	std::istringstream in(source);
	std::string line;
	int n = 0;
	while (std::getline(in, line))
	{
		++n;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		auto const hash = line.find('#');
		// "#" also starts colours (#rrggbb); only a leading one is a comment.
		auto const first = line.find_first_not_of(" \t");
		if (first == std::string::npos || (hash == first)) continue;

		std::istringstream words(line);
		std::string verb;
		words >> verb;
		std::vector<std::string> a;
		for (std::string w; words >> w;)
		{
			if (w[0] == '#' && w.size() != 4 && w.size() != 7) break; // trailing comment
			a.push_back(w);
		}
		auto need = [&](size_t count, char const* usage) {
			if (a.size() >= count) return true;
			error = ST::format("line {}: {} expects {}", n, verb, usage).to_std_string();
			return false;
		};
		auto arg = [&](size_t i, char const* def) { return i < a.size() ? a[i] : std::string(def); };

		if (verb == "move")        { if (!need(2, "X Y")) return {}; lua << "ja2.move(" << a[0] << ", " << a[1] << ")\n"; }
		else if (verb == "click")  { if (!need(2, "X Y")) return {}; lua << "ja2.click(" << a[0] << ", " << a[1] << ")\n"; }
		else if (verb == "rclick") { if (!need(2, "X Y")) return {}; lua << "ja2.rclick(" << a[0] << ", " << a[1] << ")\n"; }
		else if (verb == "key")    { if (!need(1, "NAME")) return {}; lua << "ja2.key(" << LuaQuote(a[0]) << ")\n"; }
		else if (verb == "type")
		{
			auto const pos = line.find("type") + 4;
			auto const text = line.substr(line.find_first_not_of(" \t", pos));
			lua << "ja2.type(" << LuaQuote(text) << ")\n";
		}
		else if (verb == "wait")   { if (!need(1, "MS")) return {}; lua << "ja2.wait(" << a[0] << ")\n"; }
		else if (verb == "waitpixel")
		{
			if (!need(3, "X Y #RRGGBB [tol] [timeoutMs]")) return {};
			lua << "ja2.waitPixel(" << a[0] << ", " << a[1] << ", " << LuaQuote(a[2]) << ", "
			    << arg(3, "8") << ", " << arg(4, "30000") << ")\n";
		}
		else if (verb == "waitstable")
		{
			if (!need(2, "X Y [timeoutMs]")) return {};
			lua << "ja2.waitStable(" << a[0] << ", " << a[1] << ", " << arg(2, "10000") << ")\n";
		}
		else if (verb == "assertpixel")
		{
			if (!need(3, "X Y #RRGGBB [tol]")) return {};
			lua << "ja2.check(ja2.pixelIs(" << a[0] << ", " << a[1] << ", " << LuaQuote(a[2]) << ", " << arg(3, "8")
			    << "), " << LuaQuote(ST::format("line {}: pixel ({}, {}) should be {}", n, a[0], a[1], a[2]).to_std_string())
			    << " .. ', got ' .. ja2.pixel(" << a[0] << ", " << a[1] << "))\n";
		}
		else if (verb == "screenshot") { if (!need(1, "PATH")) return {}; lua << "ja2.screenshot(" << LuaQuote(a[0]) << ")\n"; }
		else
		{
			error = ST::format("line {}: unknown command '{}'", n, verb).to_std_string();
			return {};
		}
	}
	return lua.str();
}

}
