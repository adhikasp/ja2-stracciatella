-- The overhead map, the placement and the sector card's minimap as data (issue #323, docs/plan/native-tactical.md
-- "OverheadModel"): marks, legend, hover, the click that centres the view, and the placement's rules are asserted
-- through ja2.overhead() / ja2.overheadOp() - no click path, no screenshot - and the elements are checked so the
-- drawing cannot drift from the model. The rules (scale, zones, who is next, group, spread) are unit-tested in
-- NativeUI/OverheadModel_unittest.cc.
--
-- Run: python tools/ja2ctl.py run tests/e2e/tactical_overhead.lua --isolated --res 1920x1080 [--arg res=1280x720]
local shots = require("lib.shots")
local campaign = require("lib.campaign")
local battle = require("lib.battle")

campaign.startWithTeam({ "Ivan", "Barry", "Grizzly", "Buns" }, "One Week", true)
ja2.waitIdle()
local res = (ja2.args and ja2.args[1] or ""):match("^res=(%S+)$") or "1920x1080"
ja2.setVideo{ res = res, uiscale = 1, worldzoom = 2 }
ja2.waitIdle()
local function settle() ja2.step(6) ja2.waitIdle() end
settle()

ja2.expect(ja2.exists{ id = "tac.bar" }, "the native HUD is up")

-- --- the sector card's minimap ------------------------------------------------------------------------
ja2.expect(ja2.exists{ id = "tac.radar" }, "the sector card has a minimap")
local o = ja2.overhead()
ja2.expect(not o.open, "the overhead is not open")
ja2.expect(o.picture.w == 640 and o.picture.h == 320, "the picture is the whole sector, 640x320")
ja2.expect(o.picture.revision >= 1, "the picture was drawn")
ja2.log("overhead picture took " .. string.format("%.1f", o.picture.ms) .. " ms")
ja2.expect(o.picture.ms < 250, "and drawing it does not stall a frame, took " .. o.picture.ms .. " ms")
local mercs = battle.mercs()
ja2.expect(o.legend.mercs == #mercs, "the minimap counts our mercs: " .. o.legend.mercs .. " vs " .. #mercs)
ja2.expect(o.view ~= nil and o.view.w > 20 and o.view.h > 10, "the view box is on the minimap")
shots.take("minimap.png")

-- --- the overhead -------------------------------------------------------------------------------------
local r = ja2.overheadOp("open")
ja2.expect(r.ok, "the overhead opens")
settle()
o = ja2.overhead()
ja2.expect(o.open and o.native and not o.placement, "the native overhead is up")
ja2.expect(ja2.exists{ id = "ovh.map" }, "the picture is on screen")
ja2.expect(ja2.exists{ id = "ovh.legend" }, "so is the legend")
ja2.expect(#o.squad == #mercs, "the squad list has everybody: " .. #o.squad .. " vs " .. #mercs)

local first
for _, m in ipairs(o.marks) do
	if m.kind == "merc" and m.selected then first = m end
end
ja2.expect(first, "the selected merc has a mark")
ja2.expect(first.x >= 0 and first.x < 640 and first.y >= 0 and first.y < 320, "inside the picture")
shots.take("overhead.png")

-- hover: a name
ja2.overheadOp("hover", { x = first.x, y = first.y })
settle()
o = ja2.overhead()
ja2.expect(o.hover and o.hover.kind == "merc", "hovering a dot names the merc")
ja2.expect(o.hover.title ~= "", "with his name")
shots.take("overhead_hover.png")
ja2.overheadOp("hover", { x = -1, y = -1 })
settle()
ja2.expect(ja2.overhead().hover == nil, "off the dot the name is gone")

-- a pile on the floor shows in the legend and lists what is in it
local tile = battle.merc(1).gridNo + 160 * 3
for _, item in ipairs({ 1, 2, 3 }) do ja2.debug("item", tile, item) end
settle()
o = ja2.overhead()
local pile
for _, m in ipairs(o.marks) do
	if m.kind == "item" and m.gridNo == tile then pile = m end
end
ja2.expect(pile, "a pile on the floor is a mark")
ja2.expect(o.legend.items >= 1, "and counted")
ja2.overheadOp("hover", { x = pile.x, y = pile.y })
settle()
o = ja2.overhead()
ja2.expect(o.hover and o.hover.kind == "item" and #o.hover.rows >= 1, "hovering it lists what is there")
shots.take("overhead_pile.png")
ja2.overheadOp("hover", { x = -1, y = -1 })

-- Esc leaves
ja2.key("ESC")
settle()
ja2.expect(not ja2.overhead().open, "Esc closes the overhead")
ja2.expect(ja2.state().screen == "GAME_SCREEN", "back in tactical")

-- Insert opens and closes it
ja2.key("insert")
settle()
ja2.expect(ja2.overhead().open, "Insert opens it")
ja2.key("insert")
settle()
ja2.expect(not ja2.overhead().open, "and closes it")

-- the HUD's own button
ja2.click{ id = "tac.overhead" }
settle()
ja2.expect(ja2.overhead().open, "the sector card's overhead button opens it")

-- a click centres the view there and leaves
local before = ja2.overhead().view
local target = battle.merc(1).gridNo + 160 * 10 + 10
ja2.overheadOp("click", { grid = target })
settle()
o = ja2.overhead()
ja2.expect(not o.open, "a click on the map leaves it")
ja2.expect(o.view ~= nil, "the view box is there")
local moved = math.abs(o.view.x - before.x) + math.abs(o.view.y - before.y)
ja2.expect(moved > 5, "and the view moved there: " .. moved)
shots.take("after_click.png")

-- --- the placement ---------------------------------------------------------------------------------
ja2.debug("placement", "north")
settle()
o = ja2.overhead()
ja2.expect(o.open and o.placement and o.native, "the placement opens as the native view")
ja2.expect(o.placed ~= nil, "with its state")
ja2.expect(o.placed.total == #mercs, "everybody is on the roster: " .. o.placed.total)
ja2.expect(o.placed.placed == 0 and not o.placed.canFinish, "nobody is placed yet")
ja2.expect(o.placed.lit[1] == "north", "the arrival strip is the north edge, got " .. tostring(o.placed.lit[1]))
ja2.expect(ja2.exists{ id = "ovh.done" } and ja2.exists{ id = "ovh.clear" }, "the buttons are on screen")
shots.take("placement.png")

-- Done is refused while somebody waits
r = ja2.overheadOp("done")
ja2.expect(not r.ok, "done is refused until everyone is placed")

-- a click outside the strip places nobody and says why
r = ja2.overheadOp("click", { x = 320, y = 200 })
ja2.expect(not r.ok, "outside the strip nobody is placed")
o = ja2.overhead()
ja2.expect(o.placed.placed == 0, "nobody was placed")
ja2.expect(o.placed.notice ~= "", "and it says so")

-- a click inside the strip places the selected merc and moves on
r = ja2.overheadOp("click", { x = 320, y = 12 })
ja2.expect(r.ok, "inside the strip the merc is placed: " .. tostring(r.why))
o = ja2.overhead()
ja2.expect(o.placed.placed == 1, "one merc is placed")
ja2.expect(o.placed.selected == 2, "the next one is selected, got " .. o.placed.selected)
shots.take("placement_one.png")

-- Spread places everybody along the edge
r = ja2.overheadOp("spread")
ja2.expect(r.ok, "spread")
o = ja2.overhead()
ja2.expect(o.placed.placed == o.placed.total and o.placed.canFinish, "spread places everybody")
ja2.expect(o.placed.mode == "spread", "in spread mode")
shots.take("placement_spread.png")

-- Clear picks everybody up again
ja2.overheadOp("clear")
o = ja2.overhead()
ja2.expect(o.placed.placed == 0 and o.placed.mode == "clear", "clear picks everybody up")
ja2.expect(o.placed.selected == 1, "and selects the first")

-- Group mode places the group in one click
ja2.overheadOp("group")
o = ja2.overhead()
ja2.expect(o.placed.mode == "group", "group mode")
r = ja2.overheadOp("click", { x = 300, y = 12 })
ja2.expect(r.ok, "a group is placed with one click: " .. tostring(r.why))
o = ja2.overhead()
ja2.expect(o.placed.placed >= 1, "at least the group is down")
ja2.overheadOp("group") -- back to one at a time

-- finish: place everyone, then Done
ja2.overheadOp("spread")
r = ja2.overheadOp("done")
ja2.expect(r.ok, "done once everyone is placed")
ja2.step(10)
ja2.waitIdle()
ja2.expect(not ja2.overhead().open, "the placement ends with the sector's own game")
ja2.expect(ja2.state().screen == "GAME_SCREEN", "in tactical")
shots.take("after_placement.png")
