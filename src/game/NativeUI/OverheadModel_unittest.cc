#include "OverheadModel.h"

#include <gtest/gtest.h>

using namespace OverheadModel;

TEST(OverheadModel, fitScaleIsTheLargestIntegerThatFits)
{
	EXPECT_EQ(FitScale(1280, 720), 2);   // 1280 x 640
	EXPECT_EQ(FitScale(1900, 900), 2);   // height limits it: 900 / 320 = 2.8
	EXPECT_EQ(FitScale(1920, 1100), 3);  // 1920 x 960
	EXPECT_EQ(FitScale(2560, 1440), 4);  // 2560 x 1280
	EXPECT_EQ(FitScale(3840, 2000), 6);  // 3840 x 1920
	// never below 1x, even where the picture does not fit
	EXPECT_EQ(FitScale(500, 200), 1);
	EXPECT_EQ(FitScale(0, 0), 1);
	// exactly on a multiple
	EXPECT_EQ(FitScale(640 * 3, 320 * 3), 3);
}

TEST(OverheadModel, legendCountsEveryKind)
{
	std::vector<Mark> m;
	auto add = [&m](MarkKind k, int n) { for (int i = 0; i < n; ++i) { Mark x; x.kind = k; m.push_back(x); } };
	add(MarkKind::Merc, 4);
	add(MarkKind::Enemy, 2);
	add(MarkKind::Militia, 3);
	add(MarkKind::Civilian, 5);
	add(MarkKind::Vehicle, 1);
	add(MarkKind::Item, 7);
	Legend const l = Count(m);
	EXPECT_EQ(l.mercs, 4);
	EXPECT_EQ(l.enemies, 2);
	EXPECT_EQ(l.militia, 3);
	EXPECT_EQ(l.civilians, 5);
	EXPECT_EQ(l.vehicles, 1);
	EXPECT_EQ(l.items, 7);
	EXPECT_EQ(Count({}).mercs, 0);
}

TEST(OverheadModel, markAtPicksTheNearestAndPrefersSoldiersOverItems)
{
	std::vector<Mark> m(3);
	m[0].kind = MarkKind::Item;  m[0].x = 100; m[0].y = 100;
	m[1].kind = MarkKind::Merc;  m[1].x = 103; m[1].y = 100;
	m[2].kind = MarkKind::Enemy; m[2].x = 300; m[2].y = 100;
	EXPECT_EQ(MarkAt(m, 101, 100, 6), 1) << "the soldier, though the item is nearer";
	EXPECT_EQ(MarkAt(m, 100, 100, 1), 0) << "the item, when the soldier is out of reach";
	EXPECT_EQ(MarkAt(m, 300, 104, 6), 2);
	EXPECT_EQ(MarkAt(m, 200, 200, 6), -1);
	EXPECT_EQ(MarkAt({}, 0, 0, 6), -1);
}

TEST(OverheadModel, zonesAreStripsAlongTheEdge)
{
	Rect const n = ZoneRect(Side::North);
	EXPECT_EQ(n.x, 0); EXPECT_EQ(n.y, 0); EXPECT_EQ(n.w, MAP_W); EXPECT_EQ(n.h, ZONE_DEPTH);
	Rect const s = ZoneRect(Side::South);
	EXPECT_EQ(s.y, MAP_H - ZONE_DEPTH); EXPECT_EQ(s.w, MAP_W);
	Rect const e = ZoneRect(Side::East);
	EXPECT_EQ(e.x, MAP_W - ZONE_DEPTH); EXPECT_EQ(e.h, MAP_H);
	Rect const w = ZoneRect(Side::West);
	EXPECT_EQ(w.x, 0); EXPECT_EQ(w.w, ZONE_DEPTH);
	EXPECT_EQ(ZoneRect(Side::None).w, 0);
}

TEST(OverheadModel, theLegacyEdgeTestsHold)
{
	// the legacy cursor: north y <= 40, east x >= 600, south y >= 280, west x <= 40 (inside the 640x320 box)
	EXPECT_TRUE(InZone(Side::North, 300, 40));
	EXPECT_FALSE(InZone(Side::North, 300, 41));
	EXPECT_TRUE(InZone(Side::East, 600, 100));
	EXPECT_FALSE(InZone(Side::East, 599, 100));
	EXPECT_TRUE(InZone(Side::South, 300, 280));
	EXPECT_FALSE(InZone(Side::South, 300, 279));
	EXPECT_TRUE(InZone(Side::West, 40, 100));
	EXPECT_FALSE(InZone(Side::West, 41, 100));
	// off the picture is never a zone; no side has none
	EXPECT_FALSE(InZone(Side::North, -1, 10));
	EXPECT_FALSE(InZone(Side::East, MAP_W, 10));
	EXPECT_FALSE(InZone(Side::None, 10, 10));
	// the corners belong to both strips
	EXPECT_TRUE(InZone(Side::North, 5, 5));
	EXPECT_TRUE(InZone(Side::West, 5, 5));
}

namespace
{
	std::vector<Piece> Four()
	{
		std::vector<Piece> v(4);
		for (int i = 0; i < 4; ++i)
		{
			v[i].id = 10 + i;
			v[i].name = "M" + std::to_string(i);
			v[i].side = Side::North;
			v[i].group = i < 2 ? 1 : 2;
		}
		return v;
	}
}

TEST(OverheadPlacement, beginSelectsTheFirstOneToPlace)
{
	Placement p;
	auto v = Four();
	v[0].placed = true;
	p.Begin(v, Mode::Clear);
	EXPECT_EQ(p.selected, 1);
	EXPECT_EQ(p.PlacedCount(), 1);
	EXPECT_FALSE(p.CanFinish());
	// nobody to place: nobody selected, and done is allowed
	for (auto& x : v) x.placed = true;
	p.Begin(v, Mode::Clear);
	EXPECT_EQ(p.selected, -1);
	EXPECT_TRUE(p.CanFinish());
}

TEST(OverheadPlacement, aClickPlacesTheSelectedThenMovesOn)
{
	Placement p;
	p.Begin(Four(), Mode::Clear);
	EXPECT_EQ(p.Targets(), std::vector<int>{ 0 });
	EXPECT_EQ(p.CanPlaceAt(300, 10), Placement::Why::Ok);
	EXPECT_EQ(p.CanPlaceAt(300, 200), Placement::Why::OutsideZone);
	p.SetPlaced(0, true);
	p.SelectNextUnplaced();
	EXPECT_EQ(p.selected, 1);
	p.SetPlaced(1, true);
	p.SelectNextUnplaced();
	p.SetPlaced(2, true);
	p.SelectNextUnplaced();
	p.SetPlaced(3, true);
	p.SelectNextUnplaced();
	EXPECT_EQ(p.selected, -1);
	EXPECT_TRUE(p.CanFinish());
	EXPECT_EQ(p.CanPlaceAt(300, 10), Placement::Why::NobodySelected);
}

TEST(OverheadPlacement, nextWrapsAroundToAnEarlierOne)
{
	Placement p;
	p.Begin(Four(), Mode::Clear);
	p.SetPlaced(1, true); p.SetPlaced(2, true); p.SetPlaced(3, true);
	p.Select(3);
	p.SelectNextUnplaced();
	EXPECT_EQ(p.selected, 0);
}

TEST(OverheadPlacement, groupModePlacesTheWholeGroup)
{
	Placement p;
	p.Begin(Four(), Mode::Clear);
	p.ToggleGroup();
	EXPECT_EQ(p.mode, Mode::Group);
	EXPECT_EQ(p.Targets(), (std::vector<int>{ 0, 1 }));
	p.Select(2);
	EXPECT_EQ(p.Targets(), (std::vector<int>{ 2, 3 }));
	p.ToggleGroup();
	EXPECT_EQ(p.mode, Mode::Clear);
	EXPECT_EQ(p.Targets(), std::vector<int>{ 2 }) << "back to one merc, still the selected one";
}

TEST(OverheadPlacement, clearPicksEverybodyUp)
{
	Placement p;
	p.Begin(Four(), Mode::Clear);
	for (int i = 0; i < 4; ++i) p.SetPlaced(i, true);
	p.SelectNextUnplaced();
	p.ClearAll();
	EXPECT_EQ(p.PlacedCount(), 0);
	EXPECT_EQ(p.selected, 0);
	EXPECT_EQ(p.mode, Mode::Clear);
}

TEST(OverheadPlacement, spreadPlacesEverybodyAndSelectsNobody)
{
	Placement p;
	p.Begin(Four(), Mode::Clear);
	for (int i = 0; i < 4; ++i) p.SetPlaced(i, true);
	p.SpreadDone();
	EXPECT_EQ(p.mode, Mode::Spread);
	EXPECT_EQ(p.selected, -1);
	EXPECT_TRUE(p.CanFinish());
}

TEST(OverheadPlacement, theLitEdgesFollowTheCursorMerc)
{
	Placement p;
	auto v = Four();
	v[2].side = Side::East;
	v[3].side = Side::East;
	p.Begin(v, Mode::Clear);
	// the selected one's edge
	EXPECT_EQ(p.LitSides(), std::vector<Side>{ Side::North });
	// a card under the mouse shows his edge instead
	p.Hover(3);
	EXPECT_EQ(p.LitSides(), std::vector<Side>{ Side::East });
	p.Hover(-1);
	// nobody selected: every edge someone arrives from, once
	p.Deselect();
	auto const all = p.LitSides();
	ASSERT_EQ(all.size(), 2u);
	EXPECT_EQ(all[0], Side::North);
	EXPECT_EQ(all[1], Side::East);
}

TEST(OverheadPlacement, aPlacedMercCannotBePlacedAtAnotherEdge)
{
	Placement p;
	auto v = Four();
	v[0].side = Side::West;
	p.Begin(v, Mode::Clear);
	EXPECT_EQ(p.CanPlaceAt(10, 160), Placement::Why::Ok);
	EXPECT_EQ(p.CanPlaceAt(300, 10), Placement::Why::OutsideZone) << "the north strip is not his";
}

TEST(OverheadPlacement, indexOutOfRangeIsIgnored)
{
	Placement p;
	p.Begin(Four(), Mode::Clear);
	p.Select(99);
	EXPECT_EQ(p.selected, 0);
	p.SetPlaced(-1, true);
	p.SetPlaced(40, true);
	EXPECT_EQ(p.PlacedCount(), 0);
	Placement empty;
	empty.SelectNextUnplaced();
	empty.ClearAll();
	EXPECT_FALSE(empty.CanFinish());
	EXPECT_TRUE(empty.Targets().empty());
}
