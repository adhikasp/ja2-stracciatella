#include "BattleScenario.h"
#include "ScenarioItems.h"

#include "Auto_Resolve.h"
#include "Campaign_Types.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "GridSquare.h"
#include "Handle_Items.h"
#include "Isometric_Utils.h"
#include "Item_Types.h"
#include "ItemModel.h"
#include "Items.h"
#include "JAScreens.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Random.h"
#include "Soldier_Add.h"
#include "Soldier_Control.h"
#include "Soldier_Create.h"
#include "Soldier_Tile.h"
#include "Strategic.h"
#include "StrategicMap.h"
#include "WorldDef.h"
#include "WorldMan.h"

#include <string_theory/string>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace Automation
{

namespace
{
	SoldierClass ParseClass(std::string const& name)
	{
		if (name == "administrator" || name == "admin") return SOLDIER_CLASS_ADMINISTRATOR;
		if (name == "army" || name == "troop" || name == "troops") return SOLDIER_CLASS_ARMY;
		if (name == "elite") return SOLDIER_CLASS_ELITE;
		throw std::runtime_error("unknown enemy class \"" + name + "\"");
	}

	// Move an actor onto @a grid without running sight (placement must not start combat
	// before StageBattle chooses who goes first).
	void PlaceActor(SOLDIERTYPE& s, GridNo const grid)
	{
		if (grid == NOWHERE) return;
		EVENT_SetSoldierPosition(&s, grid, SSP_NONE);
		EVENT_SetSoldierDirection(&s, s.bDirection);
		EVENT_SetSoldierDesiredDirection(&s, s.bDirection);
		s.sFinalDestination = grid;
	}

	// Face an actor a given direction (0..7); -1 leaves the generated facing alone.
	void FaceDirection(SOLDIERTYPE& s, int const direction)
	{
		if (direction < 0) return;
		EVENT_SetSoldierDirection(&s, UINT16(direction));
		EVENT_SetSoldierDesiredDirection(&s, UINT16(direction));
	}

	// One enemy to place: a grid, a class, an optional gun and facing.
	struct EnemyUnit
	{
		GridNo grid = NOWHERE;
		SoldierClass klass = SOLDIER_CLASS_ADMINISTRATOR;
		UINT16 gun = NOTHING; // NOTHING keeps the kit the generator gave him
		int direction = -1;   // -1 keeps the generated facing
	};

	// Find the per-merc setup entry, by name if the entries are named, else by position.
	sol::object MercSetup(sol::object const& our, int const index, std::string const& name)
	{
		if (!our.is<sol::table>()) return sol::nil;
		sol::table const table = our.as<sol::table>();
		int const n = int(table.size());
		auto entryName = [&](sol::object const& e) -> std::string {
			if (!e.is<sol::table>()) return std::string();
			sol::object const nm = e.as<sol::table>()["name"];
			return nm.is<std::string>() ? nm.as<std::string>() : std::string();
		};
		bool named = false;
		for (int i = 1; i <= n && !named; ++i) named = !entryName(table[i]).empty();
		if (named)
		{
			for (int i = 1; i <= n; ++i)
			{
				sol::object const e = table[i];
				if (entryName(e) == name) return e;
			}
			return sol::nil;
		}
		return index <= n ? sol::object(table[index]) : sol::object(sol::nil);
	}

	// Free, standable tiles a given distance from @a anchor, nearest to @a ideal first.
	std::vector<GridNo> FreeTilesAround(GridNo const anchor, int const ideal, int const radius)
	{
		SOLDIERTYPE dummy{};
		dummy.bLevel = 0;
		dummy.bTeam  = ENEMY_TEAM;
		dummy.sGridNo = anchor;

		std::vector<GridNo> tiles;
		for (GridNo const g : GridSquare{anchor, radius})
		{
			if (!GridNoOnVisibleWorldTile(g)) continue;
			if (!NewOKDestination(&dummy, g, TRUE, 0)) continue;
			tiles.push_back(g);
		}
		std::sort(tiles.begin(), tiles.end(), [&](GridNo const a, GridNo const b) {
			return std::abs(SpacesAway(anchor, a) - ideal) < std::abs(SpacesAway(anchor, b) - ideal);
		});
		return tiles;
	}
}

void FireAtGrid(SOLDIERTYPE* const soldier, INT16 const targetGridNo)
{
	if (!soldier) throw std::runtime_error("ja2.debug(\"fire\"): no selected merc");
	if (targetGridNo == NOWHERE) throw std::runtime_error("ja2.debug(\"fire\"): bad target tile");
	if (soldier->inv[HANDPOS].usItem == NOTHING)
		throw std::runtime_error("ja2.debug(\"fire\"): " + soldier->name.to_std_string() + " has nothing in his hand");
	// The same entry the AI fires through (AIMain.cc: AI_ACTION_FIRE_GUN): it sets the
	// attacking hand and weapon, checks the AP cost and ammo, and starts the real fire chain.
	SOLDIERTYPE const* const target = WhoIsThere2(targetGridNo, 0);
	INT8 const level = target ? target->bLevel : 0;
	HandleItem(soldier, targetGridNo, level, soldier->inv[HANDPOS].usItem, FALSE);
}

void StageBattle(sol::table const& spec)
{
	if (guiCurrentScreen != GAME_SCREEN)
		throw std::runtime_error("ja2.debug(\"battle\"): needs the tactical screen");
	if (!gWorldSector.IsValid())
		throw std::runtime_error("ja2.debug(\"battle\"): no sector is loaded");

	// Reset to the seeded state (SetRandomSeed was called at startup), so the scenario is
	// self-contained: the same spec and seed produce the same fight however the team was
	// assembled and whatever drew random numbers before this point. The plan's contract is
	// "the same scenario and seed produce the same outcome", and this makes that true
	// regardless of which screens ran earlier (e.g. the native vs legacy laptop).
	InitializeRandom();

	bool const clear  = Scenario::BoolField(spec, "clear", true);
	bool const start  = Scenario::BoolField(spec, "start", true);
	int  distance = std::max(1, Scenario::IntField(spec, "distance", 6));
	int  const defaultEnemies = std::max(0, Scenario::IntField(spec, "enemies", 10));
	std::string const defaultWeapon = Scenario::StrField(spec, "weapon", "MP5K");
	sol::object const armourObj = spec["armour"];
	std::string const defaultArmour = Scenario::ArmourLevel(armourObj, "kevlar");
	std::string const defaultClass  = Scenario::StrField(spec, "class", "administrator");
	std::string const defaultEnemyWeapon = Scenario::StrField(spec, "enemy_weapon", "");

	// `enemies` may be a count or a table with its own setup.
	sol::object const enemiesObj = spec["enemies"];
	sol::table enemySpec;
	int enemies = defaultEnemies;
	std::string enemyClass = defaultClass;
	std::string enemyWeapon = defaultEnemyWeapon;
	std::vector<GridNo> enemyGrids;
	std::vector<EnemyUnit> explicitUnits;
	bool haveEnemyGrids = false;
	if (enemiesObj.is<sol::table>())
	{
		enemySpec = enemiesObj.as<sol::table>();
		enemies = std::max(0, Scenario::IntField(enemySpec, "count", 10));
		enemyClass = Scenario::StrField(enemySpec, "class", defaultClass);
		enemyWeapon = Scenario::StrField(enemySpec, "weapon", defaultEnemyWeapon);
		distance = std::max(1, Scenario::IntField(enemySpec, "distance", distance));
		sol::object const gridsObj = enemySpec["grids"];
		if (gridsObj.is<sol::table>())
		{
			sol::table const grids = gridsObj.as<sol::table>();
			for (int i = 1; i <= int(grids.size()); ++i)
			{
				sol::object const o = grids[i];
				int g;
				if (Scenario::AsInt(o, g)) enemyGrids.push_back(GridNo(g));
			}
			haveEnemyGrids = !enemyGrids.empty();
		}
		// `units`: an explicit enemy per entry, each with its own grid/class/gun/facing.
		// They override `count`/`grids`; an entry without a grid falls back to a free tile.
		sol::object const unitsObj = enemySpec["units"];
		if (unitsObj.is<sol::table>())
		{
			sol::table const list = unitsObj.as<sol::table>();
			for (int i = 1; i <= int(list.size()); ++i)
			{
				sol::object const o = list[i];
				if (!o.is<sol::table>()) continue;
				sol::table const u = o.as<sol::table>();
				EnemyUnit unit;
				unit.grid = GridNo(Scenario::IntField(u, "grid", NOWHERE));
				unit.klass = ParseClass(Scenario::StrField(u, "class", enemyClass));
				std::string const gun = Scenario::StrField(u, "weapon", enemyWeapon);
				if (!gun.empty()) unit.gun = Scenario::ItemByName(gun);
				unit.direction = Scenario::IntField(u, "direction", -1);
				explicitUnits.push_back(unit);
			}
		}
	}
	SoldierClass const enemySoldierClass = ParseClass(enemyClass);

	if (clear)
	{
		// as ja2.debug("clearenemies"): drop the defenders and their strategic count
		FOR_EACH_IN_TEAM(e, ENEMY_TEAM) TacticalRemoveSoldier(*e);
		EliminateAllEnemies(gWorldSector);
		gTacticalStatus.fEnemyInSector = FALSE;
	}

	// Equip and place the player's team.
	sol::object const our = spec["our"];
	int anchorSum = 0, anchorN = 0, mercIndex = 0;
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (!s->bInSector || s->bLife <= 0) continue;
		++mercIndex;
		sol::object const setup = MercSetup(our, mercIndex, s->name.to_std_string());

		std::string weapon = defaultWeapon;
		std::string armour = defaultArmour;
		GridNo grid = NOWHERE;
		int direction = -1;
		sol::object items = sol::nil, stats = sol::nil;
		if (setup.is<sol::table>())
		{
			sol::table const e = setup.as<sol::table>();
			weapon = Scenario::StrField(e, "weapon", weapon);
			sol::object const setupArmour = e["armour"];
			armour = Scenario::ArmourLevel(setupArmour, armour);
			grid   = GridNo(Scenario::IntField(e, "grid", grid));
			direction = Scenario::IntField(e, "direction", direction);
			items  = e["items"];
			stats  = e["stats"];
		}

		Scenario::GiveGun(*s, Scenario::ItemByName(weapon));
		Scenario::EquipArmour(*s, armour);
		if (stats.is<sol::table>()) Scenario::ApplyStats(*s, stats.as<sol::table>());
		if (items.is<sol::table>())
		{
			sol::table const list = items.as<sol::table>();
			for (int i = 1; i <= int(list.size()); ++i)
			{
				sol::object const o = list[i];
				if (o.is<std::string>()) Scenario::GiveItem(*s, Scenario::ItemByName(o.as<std::string>()));
			}
		}
		if (grid != NOWHERE) TeleportSoldier(*s, grid, true);
		if (grid != NOWHERE || direction >= 0) FaceDirection(*s, direction);

		anchorSum += s->sGridNo;
		++anchorN;
	}
	if (anchorN == 0) throw std::runtime_error("ja2.debug(\"battle\"): no merc is in the sector");
	GridNo const anchor = INT16(anchorSum / anchorN);

	// Spawn and place the enemies. An explicit `units` list wins; otherwise the count and
	// grids/free tiles produce one default enemy each.
	UINT16 const enemyGun = enemyWeapon.empty() ? NOTHING : Scenario::ItemByName(enemyWeapon);
	std::vector<GridNo> const tiles = haveEnemyGrids ? std::vector<GridNo>{} : FreeTilesAround(anchor, distance, distance + 6);
	std::vector<EnemyUnit> units = explicitUnits;
	if (units.empty())
	{
		for (int i = 0; i < enemies; ++i)
		{
			GridNo grid = NOWHERE;
			if (haveEnemyGrids)
			{
				if (i >= int(enemyGrids.size())) break;
				grid = enemyGrids[i];
			}
			else
			{
				if (i >= int(tiles.size())) break;
				grid = tiles[i];
			}
			EnemyUnit unit;
			unit.grid = grid;
			unit.klass = enemySoldierClass;
			unit.gun = enemyGun;
			units.push_back(unit);
		}
	}
	int spawned = 0, free = 0;
	for (EnemyUnit const& unit : units)
	{
		GridNo grid = unit.grid;
		if (grid == NOWHERE)
		{
			// an entry without a grid takes the next free tile
			if (free >= int(tiles.size())) break;
			grid = tiles[free++];
		}
		SOLDIERTYPE* const e = TacticalCreateEnemySoldier(unit.klass);
		if (!e) continue;
		e->sSector = gWorldSector;
		e->sInsertionGridNo = grid;
		e->ubStrategicInsertionCode = INSERTION_CODE_GRIDNO;
		AddSoldierToSector(e);
		PlaceActor(*e, grid);
		if (unit.gun != NOTHING) Scenario::GiveGun(*e, unit.gun);
		FaceDirection(*e, unit.direction);
		++spawned;
	}
	if (spawned == 0) throw std::runtime_error("ja2.debug(\"battle\"): could not place any enemy");

	// The strategic enemy counters must match the soldiers now in the sector, or the battle
	// end/strategic code thinks the sector's garrison is inconsistent (Queen_Command warns
	// "Sector admin counters are bad" and the turn handling gets confused). Same counting as
	// ja2.debug("clearenemies")/FakeEncounter().
	SECTORINFO& si = SectorInfo[gWorldSector.AsByte()];
	si.ubNumAdmins = si.ubNumTroops = si.ubNumElites = 0;
	FOR_EACH_IN_TEAM(e, ENEMY_TEAM)
	{
		if (!e->bInSector || e->bLife == 0) continue;
		if      (e->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR) ++si.ubNumAdmins;
		else if (e->ubSoldierClass == SOLDIER_CLASS_ELITE)         ++si.ubNumElites;
		else                                                       ++si.ubNumTroops;
	}
	// the "in battle" counts the death handling decrements alongside them
	si.ubAdminsInBattle = si.ubNumAdmins;
	si.ubTroopsInBattle = si.ubNumTroops;
	si.ubElitesInBattle = si.ubNumElites;

	// Enter combat first, with the player's turn, so the scenario is deterministic (a sighting
	// pass started in real time can hand the first turn to whichever side spots the other).
	// Then let everyone look, which settles who sees whom (and fills in the sightings and
	// interrupts) while combat is already running.
	if (start && (gTacticalStatus.uiFlags & INCOMBAT) == 0) EnterCombatMode(OUR_TEAM);
	AllTeamsLookForAll(FALSE);
}

}
