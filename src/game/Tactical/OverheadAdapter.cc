#include "OverheadAdapter.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "GameLoop.h"
#include "Game_Clock.h"
#include "Handle_Items.h"
#include "Input.h"
#include "Interface.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Items.h"
#include "Map_Information.h"
#include "NativeUI.h"
#include "Overhead.h"
#include "Overhead_Map.h"
#include "Overhead_Types.h"
#include "RenderWorld.h"
#include "Soldier_Control.h"
#include "Squads.h"
#include "StrategicMap.h"
#include "Sys_Globals.h"
#include "Tactical_Placement_GUI.h"
#include "Timer_Control.h"
#include "World_Items.h"
#include "WorldDef.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <string_theory/string>

namespace
{
	using namespace OverheadModel;

	Frame           g_frame;
	OverheadPicture g_picture;
	bool            g_open = false;
	bool            g_placement = false;
	uint32_t        g_pictureAt = 0;       // GetJA2Clock() of the last redraw
	int             g_pictureKey = -1;     // what the picture was drawn for
	int             g_hoverX = -1, g_hoverY = -1;

	/** What changes the picture's identity: the sector (and level), the tileset. */
	int PictureKey()
	{
		return (gWorldSector.x * 64 + gWorldSector.y) * 8 + gWorldSector.z + int(giCurrentTilesetID) * 100000;
	}

	void DrawPicture()
	{
		auto const t0 = std::chrono::steady_clock::now();
		RenderOverheadPicture(g_picture.rgb, g_picture.w, g_picture.h);
		g_picture.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		++g_picture.revision;
		g_pictureAt = GetJA2Clock();
		g_pictureKey = PictureKey();
	}

	/** The picture again when the sector changed or it is old (doors open, fires burn, the light moves). */
	bool EnsurePicture(bool const open)
	{
		if (!gfWorldLoaded) return false;
		uint32_t const age = GetJA2Clock() - g_pictureAt;
		uint32_t const limit = open ? 3000 : 8000;
		if (g_picture.rgb.empty() || g_pictureKey != PictureKey() || age > limit) DrawPicture();
		return !g_picture.rgb.empty();
	}

	MarkKind KindOf(SOLDIERTYPE const& s)
	{
		if (s.uiStatusFlags & SOLDIER_VEHICLE) return MarkKind::Vehicle;
		switch (s.bTeam)
		{
			case OUR_TEAM:      return MarkKind::Merc;
			case ENEMY_TEAM:
			case CREATURE_TEAM: return MarkKind::Enemy;
			case MILITIA_TEAM:  return MarkKind::Militia;
			default:            return MarkKind::Civilian;
		}
	}

	/** The mark's point: the tile's centre, lifted off the ground for a soldier. */
	void PointOf(GridNo const gridNo, int const lift, int* x, int* y)
	{
		OverheadPointOfGridNo(gridNo, x, y);
		*x += 4;
		*y += 2 - lift;
	}

	void BuildMarks(Frame& f)
	{
		f.marks.clear();
		SOLDIERTYPE const* const sel = GetSelectedMan();
		bool const placing = g_placement;
		UINT32 const end = placing ? UINT32(gTacticalStatus.Team[OUR_TEAM].bLastID) + 1 : MAX_NUM_SOLDIERS;
		for (UINT32 i = 0; i < end; ++i)
		{
			SOLDIERTYPE const& s = GetMan(i);
			if (!s.bActive || !s.bInSector) continue;
			if (!placing && s.bLastRenderVisibleValue == -1 && !(gTacticalStatus.uiFlags & SHOW_ALL_MERCS)) continue;
			if (s.sGridNo == NOWHERE) continue;
			if (s.bLife == 0 && !placing) continue;
			Mark m;
			m.kind = KindOf(s);
			PointOf(s.sGridNo, 3 + s.sHeightAdjustment / 5, &m.x, &m.y);
			m.id = i;
			m.gridNo = s.sGridNo;
			m.selected = &s == sel;
			m.onRoof = s.sHeightAdjustment != 0;
			m.name = s.name.to_std_string();
			f.marks.push_back(std::move(m));
		}
		if (placing) return;

		// the piles: one mark a tile, whatever is in it
		std::set<std::pair<int, int>> seen;
		CFOR_EACH_WORLD_ITEM(wi)
		{
			if (wi.bVisible != VISIBLE && !(gTacticalStatus.uiFlags & SHOW_ALL_ITEMS)) continue;
			if (wi.sGridNo == NOWHERE) continue;
			if (!seen.insert({ wi.sGridNo, wi.ubLevel }).second) continue;
			Mark m;
			m.kind = MarkKind::Item;
			PointOf(wi.sGridNo, 0, &m.x, &m.y);
			m.id = wi.sGridNo;
			m.gridNo = wi.sGridNo;
			m.onRoof = wi.ubLevel != 0;
			f.marks.push_back(std::move(m));
		}
	}

	std::string StatusOf(SOLDIERTYPE const& s)
	{
		std::string out = "HP " + std::to_string(s.bLife);
		OBJECTTYPE const& hand = s.inv[HANDPOS];
		if (hand.usItem != NOTHING)
		{
			if (ItemModel const* const m = GCM->getItem(hand.usItem, ItemSystem::nothrow))
			{
				out += " \xC2\xB7 " + m->getShortName().to_std_string();
				if (m->isGun()) out += " " + std::to_string(hand.ubShotsLeft[0]);
			}
		}
		return out;
	}

	void BuildSquad(Frame& f)
	{
		f.squad.clear();
		SOLDIERTYPE const* const sel = GetSelectedMan();
		INT32 const squad = CurrentSquad();
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (!s->bActive || !s->bInSector || s->bLife == 0) continue;
			if (s->uiStatusFlags & SOLDIER_VEHICLE) continue;
			if (s->bAssignment != squad) continue;
			SquadRow r;
			r.id = s->ubID;
			r.face = s->ubProfile;
			r.selected = s == sel;
			r.name = s->name.to_std_string();
			r.status = StatusOf(*s);
			f.squad.push_back(std::move(r));
		}
	}

	/** Where the world view is, in picture pixels (the radar's rectangle, in the overhead's mapping). */
	void BuildViewBox(Frame& f)
	{
		constexpr float k = 1.f / 5.f;
		// the viewport in absolute world pixels, as the radar reads it
		float const l = float(gsTopLeftWorldX - SCROLL_LEFT_PADDING);
		float const t = float(gsTopLeftWorldY - SCROLL_TOP_PADDING);
		float const r = float(gsBottomRightWorldX - SCROLL_RIGHT_PADDING - SCROLL_LEFT_PADDING);
		float const b = float(gsBottomRightWorldY - SCROLL_BOTTOM_PADDING - SCROLL_TOP_PADDING);
		// what OverheadPointOfGridNo adds to a tile's own world pixels (the restricted-map offset and the margin)
		int px, py;
		OverheadPointOfGridNo(0, &px, &py);
		INT16 ax, ay;
		GetAbsoluteScreenXYFromMapPos(0, &ax, &ay);
		float const ox = float(px) - float(ax) * k, oy = float(py) - float(ay) * k;
		f.view.valid = r > l && b > t;
		f.view.x = l * k + ox;
		f.view.y = t * k + oy;
		f.view.w = (r - l) * k;
		f.view.h = (b - t) * k;
	}

	void BuildHover(Frame& f)
	{
		f.hover = Hover{};
		if (g_hoverX < 0) return;
		int const i = MarkAt(f.marks, g_hoverX, g_hoverY, 5);
		if (i < 0) return;
		Mark const& m = f.marks[i];
		Hover& h = f.hover;
		h.valid = true;
		h.kind = m.kind;
		h.x = m.x;
		h.y = m.y;
		h.id = m.id;
		if (m.kind != MarkKind::Item)
		{
			h.title = m.name;
			return;
		}
		ITEM_POOL const* const pool = GetItemPool(UINT16(m.gridNo), m.onRoof ? 1 : 0);
		if (!pool)
		{
			h.valid = false;
			return;
		}
		constexpr int MAX_ROWS = 8;
		std::vector<ItemPoolListRow> const rows = ItemPoolListRows(pool, -1);
		for (size_t k = 0; k < rows.size(); ++k)
		{
			if (int(k) >= MAX_ROWS) { h.hidden = int(rows.size()) - MAX_ROWS; break; }
			h.rows.push_back({ rows[k].item, rows[k].name, rows[k].count });
		}
		if (h.rows.empty()) h.valid = false;
	}

	void BuildFrame(bool const open)
	{
		Frame& f = g_frame;
		f.active = open;
		f.placement = g_placement;
		ST::string const id = GetSectorIDString(gWorldSector, TRUE);
		std::string full = id.to_std_string();
		auto const colon = full.find(':');
		f.sector = colon == std::string::npos ? full : full.substr(0, colon);
		f.town = colon == std::string::npos ? std::string() : full.substr(colon + 1);
		while (!f.town.empty() && f.town.front() == ' ') f.town.erase(0, 1);
		f.night = NightTime() != FALSE;
		BuildMarks(f);
		BuildSquad(f);
		BuildViewBox(f);
		BuildHover(f);
		f.texture = g_picture.revision;
	}
}

bool NativeOverheadWanted()
{
	return NativeUI::ResolveMode("tactical") == NativeUI::UiMode::Native;
}

bool NativeOverheadOpen() { return g_open; }

void OpenNativeOverhead(bool const placement)
{
	g_open = true;
	g_placement = placement;
	g_hoverX = g_hoverY = -1;
	DrawPicture();
	BuildFrame(true);
}

void CloseNativeOverhead()
{
	g_open = false;
	g_placement = false;
	g_frame = Frame{};
	g_hoverX = g_hoverY = -1;
}

void HandleNativeOverhead()
{
	if (!g_open) return;
	if (g_placement)
	{
		TacticalPlacementHandle(); // its keys and its finish
		if (!gfTacticalPlacementGUIActive) return;
	}
	else
	{
		InputAtom e;
		while (DequeueSpecificEvent(&e, KEYBOARD_EVENTS))
		{
			if (e.usEvent != KEY_DOWN) continue;
			switch (e.usParam)
			{
				case SDLK_ESCAPE:
				case SDLK_INSERT:
					KillOverheadMap();
					return;
				case 'x':
					if (e.usKeyState & ALT_DOWN) HandleShortCutExitState();
					break;
			}
		}
	}
	EnsurePicture(true);
	BuildFrame(true);
}

bool UpdateNativeMinimap()
{
	if (g_open) return !g_picture.rgb.empty();
	if (!gfWorldLoaded)
	{
		g_frame = Frame{};
		return false;
	}
	// the sector card's picture: fresh often enough to follow the mercs, rarely enough to cost nothing
	static uint32_t last = 0;
	uint32_t const now = GetJA2Clock();
	if (now - last >= 100 || g_frame.sector.empty())
	{
		last = now;
		EnsurePicture(false);
		BuildFrame(false);
	}
	return !g_picture.rgb.empty();
}

Frame const& CurrentOverheadFrame() { return g_frame; }
OverheadPicture const& CurrentOverheadPicture() { return g_picture; }

void OverheadHover(int const x, int const y)
{
	g_hoverX = x;
	g_hoverY = y;
	if (g_open) BuildHover(g_frame);
}

bool OverheadClick(int const x, int const y)
{
	if (!g_open) return false;
	if (g_placement) return NativePlacementClick(x, y);
	if (!OverheadCentreOn(x, y)) return false;
	KillOverheadMap();
	return true;
}

bool OverheadCentreOn(int const x, int const y)
{
	GridNo const pos = OverheadGridNoAtPoint(x, y, false);
	if (pos == NOWHERE) return false;
	INT16 cx, cy;
	ConvertGridNoToCenterCellXY(pos, &cx, &cy);
	SetRenderCenter(cx, cy);
	return true;
}

void OverheadLeave()
{
	if (g_open && !g_placement) KillOverheadMap();
}
