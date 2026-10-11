#include "OverheadModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace OverheadModel
{

int FitScale(float const availW, float const availH)
{
	int const k = int(std::floor(std::min(availW / MAP_W, availH / MAP_H) + 0.0001f));
	return std::max(1, k);
}

char const* KindName(MarkKind const k)
{
	switch (k)
	{
		case MarkKind::Merc:     return "merc";
		case MarkKind::Enemy:    return "enemy";
		case MarkKind::Militia:  return "militia";
		case MarkKind::Civilian: return "civilian";
		case MarkKind::Vehicle:  return "vehicle";
		case MarkKind::Item:     return "item";
	}
	return "?";
}

Legend Count(std::vector<Mark> const& marks)
{
	Legend l;
	for (Mark const& m : marks)
	{
		switch (m.kind)
		{
			case MarkKind::Merc:     ++l.mercs; break;
			case MarkKind::Enemy:    ++l.enemies; break;
			case MarkKind::Militia:  ++l.militia; break;
			case MarkKind::Civilian: ++l.civilians; break;
			case MarkKind::Vehicle:  ++l.vehicles; break;
			case MarkKind::Item:     ++l.items; break;
		}
	}
	return l;
}

int MarkAt(std::vector<Mark> const& marks, int const x, int const y, int const radius)
{
	int best = -1;
	long bestScore = std::numeric_limits<long>::max();
	for (size_t i = 0; i < marks.size(); ++i)
	{
		Mark const& m = marks[i];
		long const dx = m.x - x, dy = m.y - y;
		long const d2 = dx * dx + dy * dy;
		if (d2 > long(radius) * radius) continue;
		// a soldier wins over an item that is about as near
		long const score = d2 + (m.kind == MarkKind::Item ? long(radius) * radius : 0);
		if (score < bestScore) { bestScore = score; best = int(i); }
	}
	return best;
}

char const* SideName(Side const s)
{
	switch (s)
	{
		case Side::North: return "north";
		case Side::East:  return "east";
		case Side::South: return "south";
		case Side::West:  return "west";
		default:          return "none";
	}
}

Rect ZoneRect(Side const s)
{
	switch (s)
	{
		case Side::North: return { 0, 0, MAP_W, ZONE_DEPTH };
		case Side::South: return { 0, MAP_H - ZONE_DEPTH, MAP_W, ZONE_DEPTH };
		case Side::West:  return { 0, 0, ZONE_DEPTH, MAP_H };
		case Side::East:  return { MAP_W - ZONE_DEPTH, 0, ZONE_DEPTH, MAP_H };
		default:          return {};
	}
}

bool InZone(Side const s, int const x, int const y)
{
	if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return false;
	switch (s)
	{
		case Side::North: return y <= ZONE_DEPTH;
		case Side::South: return y >= MAP_H - ZONE_DEPTH;
		case Side::West:  return x <= ZONE_DEPTH;
		case Side::East:  return x >= MAP_W - ZONE_DEPTH;
		default:          return false;
	}
}

char const* ModeName(Mode const m)
{
	switch (m)
	{
		case Mode::Spread: return "spread";
		case Mode::Group:  return "group";
		default:           return "clear";
	}
}

// ---------------------------------------------------------------------------------------------------------------

void Placement::Begin(std::vector<Piece> list, Mode const defaultMode)
{
	pieces = std::move(list);
	mode = defaultMode;
	selected = -1;
	hovered = -1;
	for (int i = 0; i < Count(); ++i)
	{
		if (!pieces[i].placed) { selected = i; break; }
	}
}

int Placement::PlacedCount() const
{
	return int(std::count_if(pieces.begin(), pieces.end(), [](Piece const& p) { return p.placed; }));
}

void Placement::Select(int const index)
{
	if (index < 0 || index >= Count()) return;
	selected = index;
}

std::vector<int> Placement::Targets() const
{
	std::vector<int> out;
	if (selected < 0 || selected >= Count()) return out;
	if (mode != Mode::Group)
	{
		out.push_back(selected);
		return out;
	}
	int const g = pieces[selected].group;
	for (int i = 0; i < Count(); ++i)
	{
		if (pieces[i].group == g) out.push_back(i);
	}
	return out;
}

std::vector<Side> Placement::LitSides() const
{
	std::vector<Side> out;
	int const c = Cursor();
	auto add = [&out](Side s) {
		if (s != Side::None && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
	};
	if (c >= 0 && c < Count())
	{
		add(pieces[c].side);
	}
	else
	{
		for (Piece const& p : pieces) add(p.side);
	}
	return out;
}

Placement::Why Placement::CanPlaceAt(int const x, int const y) const
{
	if (selected < 0 || selected >= Count()) return Why::NobodySelected;
	return InZone(pieces[selected].side, x, y) ? Why::Ok : Why::OutsideZone;
}

void Placement::SetPlaced(int const index, bool const placed)
{
	if (index < 0 || index >= Count()) return;
	pieces[index].placed = placed;
}

void Placement::SelectNextUnplaced()
{
	int const n = Count();
	if (n == 0) return;
	int const from = std::max(0, selected);
	for (int k = 0; k < n; ++k)
	{
		int const i = (from + k) % n;
		if (!pieces[i].placed)
		{
			selected = i;
			return;
		}
	}
	selected = -1; // everybody is placed
}

void Placement::ClearAll()
{
	for (Piece& p : pieces) p.placed = false;
	mode = Mode::Clear;
	selected = pieces.empty() ? -1 : 0;
}

void Placement::SpreadDone()
{
	mode = Mode::Spread;
	selected = -1;
}

void Placement::ToggleGroup()
{
	if (mode == Mode::Group)
	{
		mode = Mode::Clear;
		return;
	}
	mode = Mode::Group;
	selected = -1;
	for (int i = 0; i < Count(); ++i)
	{
		if (!pieces[i].placed) { selected = i; break; }
	}
	if (selected < 0 && !pieces.empty()) selected = 0;
}

}
