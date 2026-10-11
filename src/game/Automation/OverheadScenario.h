#pragma once

#include "AutomationLua.h"

#include <string>

/** @file
 * The Lua door onto the overhead map and the placement (issue #323, docs/plan/native-tactical.md "OverheadModel").
 * The rules are in `NativeUI/OverheadModel.h`, the adapter in `Tactical/OverheadAdapter.cc` and the placement in
 * `TileEngine/Tactical_Placement_GUI.cc`; this reads what the view shows as data and makes the view's choices as
 * calls, so a click path or a screenshot is never needed to assert a result.
 *
 *   ja2.overhead()  { open, native, placement,
 *                     sector, town, picture = { w, h, revision, ms },
 *                     legend = { mercs, enemies, militia, civilians, vehicles, items },
 *                     marks = { { kind, x, y, id, gridNo, name, selected, onRoof }... }   (map pixels)
 *                     view = { x, y, w, h } | nil,
 *                     squad = { { id, name, status, selected }... },
 *                     hover = { kind, id, title, x, y, rows = { { item, name, count }... }, hidden } | nil,
 *                     placed = { mode = "clear"|"spread"|"group", selected, hovered, placed, total, canFinish,
 *                                notice, lit = { "north"... },
 *                                pieces = { { id, name, side, group, placed }... } } | nil }
 *   ja2.overheadOp(op, spec)  returns { ok, why }. op is one of
 *        "open" {}                      the overhead (Insert)
 *        "close" {}                     back to the world (Esc / Insert)
 *        "hover" { x, y } | { grid }    the pointer over a picture pixel or a tile
 *        "click" { x, y } | { grid }    a click there: centres the view / places the selected merc
 *        "select" { index } | { id }    placement: a roster card (index counts from 1)
 *        "clear" | "spread" | "group" | "done"   placement: the buttons
 *   ja2.debug("placement")              starts the placement in the current sector
 */
namespace Automation
{
	sol::table OverheadState(sol::state& L);
	sol::table OverheadOp(sol::state& L, std::string const& op, sol::optional<sol::table> spec);
}
