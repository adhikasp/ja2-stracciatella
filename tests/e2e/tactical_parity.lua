-- Parity tour of the native tactical HUD (docs/ui/tactical.md, section 9) by element id, with game-state assertions.
-- Below 1280x720 the legacy HUD runs (tactical_hud.lua covers it) and the tour only checks that.
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "?.lua;" .. package.path
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.newGame()
campaign.hireFromAim("Ivan", "One Week", true)
campaign.landInArulco()
ja2.waitIdle()

local function vm() return ja2.viewModel("tactical") end
local function slot(i) return { id = ("tac.inv.slot[%d]"):format(math.floor(i)) } end
local function hand() return ja2.state().mercs[1] end

if not ja2.nativeUi().running or ja2.screenSize().w < 1280 then
	ja2.expect(not ja2.exists{ id = "tac.bar" }, "below 1280x720 the legacy tactical HUD runs")
	shots.take("legacy.png", "small")
	return
end

-- squad bar: the merc's card, his face and what is in his hand
ja2.expect(ja2.exists{ id = "tac.bar" }, "the native squad bar is shown")
local v = vm()
ja2.expect(v.cards[1].name == "Ivan", "card 1 is Ivan")
ja2.expect(v.cards[1].ammo ~= "", "Ivan's gun and ammo are on his card")
ja2.expect(v.sector == "A9", "the sector card says A9, got " .. tostring(v.sector))
shots.take("hud.png", "small")

-- I7, A1: select by card
ja2.click{ id = "tac.squad[0]" }
ja2.waitIdle()
ja2.expect(vm().cards[1].sel, "clicking the card selects Ivan")

-- A4: stance buttons press the legacy keys
ja2.click{ id = "tac.stance.crouch" }
ja2.wait(1500)
ja2.expect(vm().stance == "crouch", "the crouch button crouches, got " .. vm().stance)
ja2.click{ id = "tac.stance.stand" }
ja2.wait(1500)
ja2.expect(vm().stance == "stand", "the stand button stands, got " .. vm().stance)

-- A6, A7: stealth and burst toggle the merc's own flags
local stealth = vm().stealth
ja2.click{ id = "tac.stealth" }
ja2.waitIdle()
ja2.expect(vm().stealth ~= stealth, "the stealth button toggles stealth")
ja2.click{ id = "tac.stealth" }
ja2.waitIdle()

-- A2: details and inventory
ja2.click{ id = "tac.inventory" }
ja2.waitIdle()
ja2.expect(vm().detail and vm().d_name == "Ivan", "the detail panel shows Ivan")
shots.take("detail.png", "small")

-- A14: the item description of the gun in his hand, unload and reload through the legacy rules
ja2.click({ id = "tac.inv.slot[5]" }, { button = "right" })
ja2.waitIdle()
ja2.expect(vm().desc, "right click on the hand slot opens the item description")
ja2.expect(vm().x_name ~= "", "the description names the item")
ja2.expect(#vm().x_atts >= 3 and #vm().x_atts <= 4, "the weapon's typed attachment slots")
shots.take("desc.png", "small")
local before = vm().cards[1].ammo
ja2.click{ id = "tac.desc.unload" }
ja2.waitIdle()
ja2.expect(ja2.inventory().last.action == "unload", "the inventory core recorded the unload, got " .. tostring(ja2.inventory().last.action))
ja2.expect(ja2.inventory().hand ~= nil, "the core holds the magazine in the hand")
ja2.expect(vm().cards[1].ammo:match("^0/"), "unload empties the gun (" .. before .. " -> " .. vm().cards[1].ammo .. ")")
-- the magazine is in the cursor now: put it back into the gun
ja2.click{ id = "tac.inv.slot[5]" }
ja2.waitIdle()
ja2.expect(vm().cards[1].ammo == before, "clicking the gun with the magazine reloads it (" .. vm().cards[1].ammo .. ")")
if vm().desc then ja2.click{ id = "tac.desc.done" } ja2.waitIdle() end

-- A13: pick an item up and put it into an empty small pocket
local from, to
for _, s in ipairs(vm().small) do
	if not s.empty and not from then from = s.idx end
	if s.empty and not to then to = s.idx end
end
if from and to then
	ja2.click(slot(from))
	ja2.waitIdle()
	ja2.click(slot(to))
	ja2.waitIdle()
	local moved
	for _, s in ipairs(vm().small) do if s.idx == to then moved = not s.empty end end
	ja2.expect(moved, "the item moved from pocket " .. from .. " to pocket " .. to)
	local o = ja2.inventory().last
	ja2.expect(o.ok and o.action == "put", "the core recorded the move as a put, got " .. tostring(o.action))
	ja2.expect(ja2.inventory().hand == nil, "the hand is empty after the move")
end

-- A15: withdraw money from the account into the hand, then into an empty pocket
local money = ja2.state().money
ja2.click{ id = "tac.inv.money" }
ja2.waitIdle()
ja2.expect(vm().desc and vm().desc_money, "the cash button opens the money split")
ja2.click{ id = "tac.money.add100" }
ja2.waitIdle()
ja2.expect(vm().m_removing:find("100"), "+100 adds 100 to the amount (" .. vm().m_removing .. ")")
shots.take("money.png", "small")
ja2.click{ id = "tac.desc.done" }
ja2.waitIdle()
-- The money is on the cursor now. The pockets are typed, so not every empty one takes
-- it - the belt's magazine pockets refuse money - and the panel cannot be closed while
-- the cursor holds something: offer it to the empty pockets until one takes it.
local function occupied()
	local n = 0
	for _, group in ipairs({ vm().big, vm().small }) do
		for _, s in ipairs(group) do if not s.empty then n = n + 1 end end
	end
	return n
end
local carried = occupied()
for _, group in ipairs({ vm().big, vm().small }) do
	for _, s in ipairs(group) do
		if occupied() == carried and s.empty then
			ja2.click(slot(s.idx))
			ja2.waitIdle()
		end
	end
end
ja2.expect(occupied() > carried, "an empty pocket takes the money")
ja2.expect(ja2.state().money == money - 100, "the account lost 100 (" .. money .. " -> " .. ja2.state().money .. ")")

ja2.click{ id = "tac.detail.close" }
ja2.waitIdle()
ja2.expect(not vm().detail, "the detail panel closes")

-- I15, I18: the sector card has the minimap, and its overhead button opens the native overhead map (Esc closes it;
-- tactical_overhead.lua covers marks, hover, click and the placement)
ja2.expect(ja2.exists{ id = "tac.radar" }, "the sector card shows the minimap")
ja2.expect(ja2.overhead().legend.mercs == 1, "the minimap counts Ivan")
ja2.click{ id = "tac.overhead" }
ja2.waitIdle()
ja2.expect(ja2.overhead().open and ja2.overhead().native, "the overhead button opens the native overhead")
ja2.expect(ja2.exists{ id = "ovh.map" }, "with its picture")
shots.take("overhead.png", "small")
ja2.key("ESC")
ja2.waitIdle()
ja2.expect(not ja2.overhead().open, "Esc closes it")

-- I13: H opens the message log
ja2.key("h")
ja2.waitIdle()
ja2.expect(vm().log_open and #vm().log > 0, "H opens the message log with the messages")
shots.take("log.png", "small")
ja2.key("h")
ja2.waitIdle()
ja2.expect(not vm().log_open, "H closes it")

-- A18: the action menu (a right click held on the terrain): Move and Act with keys and AP costs
ja2.move(math.floor(ja2.screenSize().w / 2), math.floor(ja2.screenSize().h / 4))
ja2.mousedown("right")
ja2.wait(2500)
local v = vm()
if v.menu_open then
	local items = 0
	for _, m in ipairs(v.menu) do if m.kind == "item" then items = items + 1 end end
	ja2.expect(items >= 8, "the action menu has its rows, got " .. items)
	ja2.expect(v.menu_title == "Ivan", "the action menu is titled with the merc, got " .. tostring(v.menu_title))
	shots.take("menu_action.png", "small")
	ja2.mouseup("right")
	ja2.wait(300)
	-- the Walk row (group Move) presses the legacy button and closes the menu
	ja2.click{ id = "tac.menu.item[0]" }
	ja2.waitIdle()
	ja2.expect(not vm().menu_open, "choosing a row closes the action menu")
else
	ja2.mouseup("right")
	ja2.waitIdle()
	ja2.expect(false, "a right click held on the terrain opens the action menu")
end

-- A19: the door menu: what can be done with a door, the AP costs, and why a choice is off
ja2.debug("doormenu")
ja2.waitIdle()
v = vm()
if v.menu_open then
	ja2.expect(v.menu_title == "Door", "the door menu is titled Door, got " .. tostring(v.menu_title))
	local lockpick
	for _, m in ipairs(v.menu) do if m.kind == "item" and m.label:find("ockpick") then lockpick = m end end
	ja2.expect(lockpick, "the door menu has the lockpick row")
	if lockpick and lockpick.disabled then
		ja2.expect(lockpick.why ~= "", "a disabled door action says why (" .. tostring(lockpick.why) .. ")")
	end
	shots.take("menu_door.png", "small")
	-- Examine for traps: the row runs the legacy door action and closes the menu
	ja2.click{ id = "tac.menu.item[1]" }
	ja2.waitIdle()
	ja2.expect(not vm().menu_open, "choosing a door action closes the menu")
end

-- A17: the pick-up menu: the items on the ground, All, Take and Cancel
ja2.debug("pickupmenu")
ja2.waitIdle()
v = vm()
ja2.expect(v.pick_open and #v.pick >= 3, "the pick-up menu lists the items on the ground")
ja2.expect(v.pick_title == "Pick up", "the pick-up menu is titled Pick up, got " .. tostring(v.pick_title))
shots.take("menu_pickup.png", "small")
ja2.click{ id = "tac.pick.item[0]" }
ja2.waitIdle()
v = vm()
ja2.expect(v.pick[1].sel, "clicking a row selects it")
ja2.expect(v.pick_ok == "Take 1", "the OK button says Take 1, got " .. tostring(v.pick_ok))
ja2.click{ id = "tac.pickup.ok" }
ja2.waitIdle()
ja2.expect(not vm().pick_open, "take closes the pick-up menu")

-- I14, A10: turn-based combat (Omerta has enemies) and the turn banner
ja2.click{ id = "tac.endturn" }
ja2.wait(2000)
ja2.waitIdle()
if ja2.state().tactical.inCombat then
	ja2.expect(vm().combat and vm().cards[1].ap > 0, "in combat the card shows the AP")
	shots.take("combat.png", "small")
end

-- layout at bigger UI scales
for _, s in ipairs({ 1.5, 2 }) do
	ja2.setUiScale(s)
	ja2.waitIdle()
	shots.take(("scale_%d.png"):format(math.floor(s * 100)))
end
ja2.setUiScale(1)
