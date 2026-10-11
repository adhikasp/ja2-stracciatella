#pragma once
// The overhead map and the placement mode as data (issue #323, docs/plan/native-tactical.md "OverheadModel").
// The legacy code (TileEngine/Overhead_Map.cc, Tactical_Placement_GUI.cc) drew a 640x320 picture, markers and
// buttons into the canvas and mixed the placement rules into the button callbacks. This core says what the view
// is made of and what the placement rules are. No globals, no RmlUi: the adapter (Tactical/OverheadAdapter.cc)
// fills a Frame from the legacy globals and carries out what the placement model decides; the view
// (NativeUI/TacticalOverhead.cc) draws the frame; Lua reads it as data (ja2.overhead()).
//
// Coordinates are *map pixels*: the whole sector in the overhead's small tiles, MAP_W x MAP_H, origin top-left.
// The picture is shown at the largest integer scale that fits the view (FitScale), never stretched.

#include <cstdint>
#include <string>
#include <vector>

namespace OverheadModel
{
	constexpr int MAP_W = 640, MAP_H = 320;

	// ------------------------------------------------------------------ the view

	/** The largest integer k >= 1 for which a MAP_W x MAP_H picture fits in availW x availH (output pixels). */
	int FitScale(float availW, float availH);

	struct Rect { int x = 0, y = 0, w = 0, h = 0; };

	// ------------------------------------------------------------------ marks

	enum class MarkKind : uint8_t { Merc, Enemy, Militia, Civilian, Vehicle, Item };
	char const* KindName(MarkKind);

	struct Mark
	{
		MarkKind    kind = MarkKind::Merc;
		int         x = 0, y = 0; // map pixels, where the dot is centred
		int         id = -1;      // soldier id, or the item's grid number
		int         gridNo = -1;
		bool        selected = false;
		bool        onRoof = false;
		std::string name;         // mercs and others: the name plate; items: empty
	};

	struct Legend
	{
		int mercs = 0, enemies = 0, militia = 0, civilians = 0, vehicles = 0, items = 0;
	};
	/** How many marks of each kind. */
	Legend Count(std::vector<Mark> const& marks);

	/** The mark under (x, y) within @a radius map pixels: the nearest, a soldier before an item. -1: none. */
	int MarkAt(std::vector<Mark> const& marks, int x, int y, int radius);

	// ------------------------------------------------------------------ placement zones

	/** The edge a merc arrives from. */
	enum class Side : uint8_t { None, North, East, South, West };
	char const* SideName(Side);

	/** How deep the arrival strip is, from the edge, in map pixels (what the legacy cursor allowed). */
	constexpr int ZONE_DEPTH = 40;

	/** The strip along the edge (map pixels); empty for None. */
	Rect ZoneRect(Side);
	/** A point is in the strip (the edge pixel included, as the legacy test was). */
	bool InZone(Side, int x, int y);

	// ------------------------------------------------------------------ placement

	/** What a click on the map does. Clear: place the selected merc. Group: place the selected merc's whole group.
	 * Spread: everybody was placed along their edge at random (the click places nothing more). Saved between
	 * battles, as the legacy button was. */
	enum class Mode : uint8_t { Clear, Spread, Group };
	char const* ModeName(Mode);

	struct Piece
	{
		int         id = -1;   // soldier id
		Side        side = Side::None;
		int         group = 0; // squad / group id
		bool        placed = false;
		int         face = -1; // the face picture (the profile id)
		std::string name;
	};

	/** The placement session's state and rules. The adapter performs the placing (it needs the map's edgepoints);
	 * this says which pieces a click is for, where it may land, who is next and whether Done is allowed. */
	struct Placement
	{
		std::vector<Piece> pieces;
		Mode mode = Mode::Clear;
		int  selected = -1; // index into pieces, or -1
		int  hovered = -1;  // a roster card under the mouse, or -1

		/** Starts a session for @a list; selects the first one still to place (Group mode shows his group). */
		void Begin(std::vector<Piece> list, Mode defaultMode);

		int  Count() const { return int(pieces.size()); }
		int  PlacedCount() const;
		bool EveryonePlaced() const { return !pieces.empty() && PlacedCount() == Count(); }
		/** Done is allowed once everyone is placed. */
		bool CanFinish() const { return EveryonePlaced(); }

		/** Roster click. Out of range: nothing. */
		void Select(int index);
		void Deselect() { selected = -1; }
		void Hover(int index) { hovered = index; }

		/** The pieces a click on the map is for: the selected one, or in Group mode everybody of his group. */
		std::vector<int> Targets() const;
		/** The piece whose edge is lit: the hovered card, else the selected one, else none (-1). */
		int  Cursor() const { return hovered >= 0 ? hovered : selected; }
		/** The edges that are lit: the cursor piece's, or every edge somebody arrives from when there is none. */
		std::vector<Side> LitSides() const;

		/** May the selected piece be put at this map point? Why not is a legacy string id. */
		enum class Why : uint8_t { Ok, NobodySelected, OutsideZone };
		Why  CanPlaceAt(int x, int y) const;

		void SetPlaced(int index, bool placed);
		/** After a successful click: the next piece still to place, from the selected one onward and wrapping;
		 * nobody (-1) when everyone is placed. */
		void SelectNextUnplaced();

		// the three buttons
		/** Clear: everybody is picked up again, the first is selected (Clear mode). */
		void ClearAll();
		/** Spread: the adapter placed everybody (SetPlaced for each); nobody selected (Spread mode). */
		void SpreadDone();
		/** Group: toggles Group mode; entering it selects the first piece still to place. */
		void ToggleGroup();
	};

	// ------------------------------------------------------------------ everything for one frame

	struct ViewBox { bool valid = false; float x = 0, y = 0, w = 0, h = 0; };

	/** One of our squad, for the side list. */
	struct SquadRow
	{
		int         id = -1;
		int         face = -1;
		bool        selected = false;
		std::string name, status;
	};

	/** What is under the pointer: a soldier's name, or the items of a pile. */
	struct HoverRow { int item = 0; std::string name; int count = 1; };
	struct Hover
	{
		bool        valid = false;
		MarkKind    kind = MarkKind::Merc;
		int         x = 0, y = 0;   // where the mark is (map pixels)
		int         id = -1;
		std::string title;          // a soldier's name, or the tile of a pile
		std::vector<HoverRow> rows; // a pile's items
		int         hidden = 0;     // items past the first few
	};

	struct Frame
	{
		bool                 active = false;     // the native view is up
		bool                 placement = false;  // placement mode of the view
		std::string          sector;             // "A9"
		std::string          town;
		std::vector<Mark>    marks;
		std::vector<SquadRow> squad;
		Hover                hover;
		ViewBox              view;               // what the world view shows now (map pixels)
		bool                 night = false;
		int                  texture = 0;        // changes when the picture was redrawn
	};
}
