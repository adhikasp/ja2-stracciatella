#include "OverheadScenario.h"

#include "OverheadAdapter.h"
#include "Overhead_Map.h"
#include "Tactical_Placement_GUI.h"

#include <stdexcept>

namespace Automation
{

namespace
{
	/** A picture pixel from { x, y } or { grid }. */
	bool PointOf(sol::table const& s, int* x, int* y)
	{
		sol::optional<int> const grid = s["grid"];
		if (grid)
		{
			OverheadPointOfGridNo(*grid, x, y);
			// a tile's centre, as the marks are
			*x += 4;
			*y += 2;
			return true;
		}
		sol::optional<int> const px = s["x"], py = s["y"];
		if (!px || !py) return false;
		*x = *px;
		*y = *py;
		return true;
	}
}

sol::table OverheadState(sol::state& L)
{
	using namespace OverheadModel;
	Frame const& f = CurrentOverheadFrame();
	OverheadPicture const& pic = CurrentOverheadPicture();
	sol::table t = L.create_table();
	t["open"] = f.active;
	t["native"] = OverheadIsNative();
	t["placement"] = f.placement;
	t["sector"] = f.sector;
	t["town"] = f.town;
	t["night"] = f.night;

	sol::table p = L.create_table();
	p["w"] = pic.w;
	p["h"] = pic.h;
	p["revision"] = pic.revision;
	p["ms"] = pic.ms;
	t["picture"] = p;

	Legend const lg = Count(f.marks);
	sol::table l = L.create_table();
	l["mercs"] = lg.mercs;
	l["enemies"] = lg.enemies;
	l["militia"] = lg.militia;
	l["civilians"] = lg.civilians;
	l["vehicles"] = lg.vehicles;
	l["items"] = lg.items;
	t["legend"] = l;

	sol::table marks = L.create_table();
	int n = 1;
	for (Mark const& m : f.marks)
	{
		sol::table r = L.create_table();
		r["kind"] = std::string(KindName(m.kind));
		r["x"] = m.x;
		r["y"] = m.y;
		r["id"] = m.id;
		r["gridNo"] = m.gridNo;
		r["name"] = m.name;
		r["selected"] = m.selected;
		r["onRoof"] = m.onRoof;
		marks[n++] = r;
	}
	t["marks"] = marks;

	if (f.view.valid)
	{
		sol::table v = L.create_table();
		v["x"] = f.view.x;
		v["y"] = f.view.y;
		v["w"] = f.view.w;
		v["h"] = f.view.h;
		t["view"] = v;
	}

	sol::table squad = L.create_table();
	n = 1;
	for (SquadRow const& r : f.squad)
	{
		sol::table q = L.create_table();
		q["id"] = r.id;
		q["name"] = r.name;
		q["status"] = r.status;
		q["selected"] = r.selected;
		squad[n++] = q;
	}
	t["squad"] = squad;

	if (f.hover.valid)
	{
		sol::table h = L.create_table();
		h["kind"] = std::string(KindName(f.hover.kind));
		h["id"] = f.hover.id;
		h["title"] = f.hover.title;
		h["x"] = f.hover.x;
		h["y"] = f.hover.y;
		h["hidden"] = f.hover.hidden;
		sol::table rows = L.create_table();
		int k = 1;
		for (HoverRow const& r : f.hover.rows)
		{
			sol::table row = L.create_table();
			row["item"] = r.item;
			row["name"] = r.name;
			row["count"] = r.count;
			rows[k++] = row;
		}
		h["rows"] = rows;
		t["hover"] = h;
	}

	if (NativePlacementActive())
	{
		Placement const& pl = NativePlacementState();
		sol::table q = L.create_table();
		q["mode"] = std::string(ModeName(pl.mode));
		q["selected"] = pl.selected + 1; // Lua counts from 1; 0 = nobody
		q["hovered"] = pl.hovered + 1;
		q["placed"] = pl.PlacedCount();
		q["total"] = pl.Count();
		q["canFinish"] = pl.CanFinish();
		q["notice"] = NativePlacementNotice();
		sol::table lit = L.create_table();
		int k = 1;
		for (Side const s : pl.LitSides()) lit[k++] = std::string(SideName(s));
		q["lit"] = lit;
		sol::table pieces = L.create_table();
		k = 1;
		for (Piece const& pc : pl.pieces)
		{
			sol::table r = L.create_table();
			r["id"] = pc.id;
			r["name"] = pc.name;
			r["side"] = std::string(SideName(pc.side));
			r["group"] = pc.group;
			r["placed"] = pc.placed;
			pieces[k++] = r;
		}
		q["pieces"] = pieces;
		t["placed"] = q;
	}
	return t;
}

sol::table OverheadOp(sol::state& L, std::string const& op, sol::optional<sol::table> spec)
{
	sol::table const s = spec ? *spec : L.create_table();
	bool ok = false;
	std::string why;

	if (op == "open")
	{
		if (!InOverheadMap()) GoIntoOverheadMap();
		ok = InOverheadMap();
	}
	else if (op == "close")
	{
		if (!InOverheadMap()) why = "the overhead is not open";
		else if (NativePlacementActive()) why = "the placement ends with done";
		else { KillOverheadMap(); ok = true; }
	}
	else if (op == "hover")
	{
		int x = -1, y = -1;
		if (!PointOf(s, &x, &y)) throw std::runtime_error("ja2.overheadOp(\"hover\"): give x and y, or grid");
		OverheadHover(x, y);
		ok = true;
	}
	else if (op == "click")
	{
		int x = 0, y = 0;
		if (!PointOf(s, &x, &y)) throw std::runtime_error("ja2.overheadOp(\"click\"): give x and y, or grid");
		ok = OverheadClick(x, y);
		if (!ok) why = NativePlacementActive() ? NativePlacementNotice() : "nothing there";
	}
	else if (op == "select")
	{
		if (!NativePlacementActive()) throw std::runtime_error("ja2.overheadOp(\"select\"): no placement is open");
		int index = -1;
		if (sol::optional<int> const i = s["index"]) index = *i - 1;
		else if (sol::optional<int> const id = s["id"])
		{
			auto const& pieces = NativePlacementState().pieces;
			for (size_t k = 0; k < pieces.size(); ++k) if (pieces[k].id == *id) index = int(k);
		}
		if (index < 0 || index >= NativePlacementState().Count()) why = "no such merc";
		else { NativePlacementSelect(index); ok = true; }
	}
	else if (op == "clear" || op == "spread" || op == "group" || op == "done")
	{
		if (!NativePlacementActive()) throw std::runtime_error("ja2.overheadOp(\"" + op + "\"): no placement is open");
		if (op == "clear")       { NativePlacementClear(); ok = true; }
		else if (op == "spread") { NativePlacementSpread(); ok = true; }
		else if (op == "group")  { NativePlacementGroup(); ok = true; }
		else
		{
			ok = NativePlacementDone();
			if (!ok) why = "somebody is not placed";
		}
	}
	else
	{
		throw std::runtime_error("ja2.overheadOp: unknown op \"" + op + "\"");
	}
	sol::table r = L.create_table();
	r["ok"] = ok;
	r["why"] = why;
	return r;
}

}
