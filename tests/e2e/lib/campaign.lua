-- Reusable steps for e2e scripts: get from the main menu to a running
-- campaign through the real UI. Load with: local campaign = require("lib.campaign")

local campaign = {}

-- A rect given in classic 640x480 coordinates, moved to where that area sits
-- on the current screen (the classic screens are centred at wider resolutions).
function campaign.std(r)
	local s = ja2.screenSize()
	return {x = r.x + s.stdX, y = r.y + s.stdY, w = r.w, h = r.h}
end

-- True while the native laptop is up (a screen with ui_mode native, 1280x720 and larger).
function campaign.nativeLaptop()
	return ja2.state().screen == "LAPTOP_SCREEN" and ja2.nativeUi().screen == "laptop"
end

-- The number of mercs the laptop shows (the legacy "Mercs: N" under Personnel, or the native system bar).
function campaign.laptopMercs()
	if campaign.nativeLaptop() then return tonumber(ja2.viewModel("laptop").team) end
	for n = 0, 18 do
		if ja2.exists("Mercs: " .. n) then return n end
	end
	return nil
end

-- Close the first-visit help overlay (ticking "don't show again") if it is up.
function campaign.dismissHelp()
	ja2.waitIdle()
	if not ja2.exists{text = "Close help", exact = true} then return false end
	if ja2.exists("Don't show me this type of help anymore") then
		ja2.click("Don't show me this type of help anymore")
	end
	ja2.click{text = "Close help", exact = true}
	ja2.waitIdle()
	return true
end

-- Main menu -> new game with default settings -> laptop, popups dismissed.
function campaign.newGame()
	ja2.waitScreen("MAINMENU_SCREEN")
	ja2.click("New Game")
	ja2.waitScreen("GAME_INIT_OPTIONS_SCREEN")
	ja2.click{text = "Ok", exact = true}
	-- The difficulty confirmation.
	ja2.waitScreen("MSG_BOX_SCREEN")
	ja2.click{text = "YES", exact = true}
	ja2.waitScreen("LAPTOP_SCREEN")
	campaign.dismissLaptopPopups()
end

-- The laptop greets you with first-visit help and "You have new mail...",
-- in either order; close both.
function campaign.dismissLaptopPopups()
	for _ = 1, 3 do
		campaign.dismissHelp()
		if ja2.exists("You have new mail...") then
			ja2.click{text = "Yes", exact = true}
			ja2.waitIdle()
		end
	end
end

-- In the laptop: hire an A.I.M. merc by the name shown under their portrait.
-- With equipment = true the merc brings his A.I.M. gear ("Buy Equipment").
function campaign.hireFromAim(name, contract, equipment)
	if campaign.nativeLaptop() then
		-- the native laptop (docs/ui/laptop.md): the A.I.M. members grid, the member page, the docked hire panel
		ja2.click{id = "laptop.app.web"}
		ja2.waitIdle()
		-- a previous hire leaves the site on that merc's member page: go back to the grid first
		ja2.click{id = "aim.nav.members"}
		ja2.waitIdle()
		-- the grid is sorted by price and scrolls; a merc below the fold is not clickable, so
		-- scroll back to the top and down until their card is in view
		local sz = ja2.screenSize()
		local wx, wy = math.floor(sz.w * 0.62), math.floor(sz.h * 0.55)
		ja2.wheel(60, wx, wy)
		ja2.waitIdle()
		for _ = 1, 40 do
			if ja2.exists{text = name, exact = true} then break end
			ja2.wheel(-3, wx, wy)
			ja2.waitIdle()
		end
		ja2.click{text = name, exact = true}
		ja2.waitIdle()
		ja2.click{id = "aim.contact"}
		ja2.waitFor{id = "aim.hire.tohire"}
		ja2.click{id = "aim.hire.tohire"}
		ja2.waitIdle()
		ja2.click{text = contract or "One Week", exact = true}
		ja2.click{id = equipment and "aim.hire.gear.buy" or "aim.hire.gear.none"}
		ja2.click{id = "aim.hire.transfer"}
		ja2.waitFor{id = "aim.hire.done"}
		ja2.click{id = "aim.hire.done"}
		ja2.waitIdle()
		campaign.acceptMessageBox("should arrive")
		return
	end
	ja2.click("Web")
	ja2.waitIdle()
	ja2.click("A.I.M.")
	ja2.waitIdle()
	ja2.click{text = "Members", exact = true, within = campaign.std{x = 200, y = 200, w = 350, h = 150}}
	ja2.waitIdle()
	ja2.click("mug shot index")
	ja2.waitIdle()
	ja2.click{text = name, exact = true}
	ja2.waitIdle()
	ja2.click("Contact")
	ja2.waitFor("HIRE")
	ja2.click("HIRE")
	ja2.waitIdle()
	ja2.click(contract or "One Week")
	ja2.click(equipment and "Buy Equipment" or "No Equipment")
	ja2.click("TRANSFER FUNDS")
	ja2.waitFor("TRANSFER SUCCESSFUL")
	ja2.click{text = "OK", exact = true}
	ja2.waitIdle()
	-- "<name> should arrive at the designated drop-off point ..."
	campaign.acceptMessageBox("should arrive")
end

-- If a message box containing `pattern` is open, press its OK button.
function campaign.acceptMessageBox(pattern)
	ja2.waitIdle()
	local s = ja2.state()
	if not (s.messageBox and s.messageBoxText and s.messageBoxText:find(pattern, 1, true)) then return false end
	ja2.click{text = "OK", exact = true}
	ja2.waitIdle()
	return true
end

-- Laptop -> map screen -> run time until the hired mercs land in tactical.
function campaign.landInArulco()
	ja2.click("Shut Down")
	ja2.waitScreen("MAP_SCREEN")
	campaign.dismissHelp()
	-- Compressing time runs the clock until the helicopter drop.
	ja2.click("Time Compress (+)")
	ja2.waitScreen("GAME_SCREEN", 600000)
	campaign.dismissHelp()
end

-- Everything above: a fresh campaign with a team of mercs standing in Omerta.
function campaign.startWithTeam(names, contract, equipment)
	campaign.newGame()
	for _, name in ipairs(names) do campaign.hireFromAim(name, contract, equipment) end
	campaign.landInArulco()
	return ja2.state()
end

-- A fresh campaign with one merc standing in Omerta.
function campaign.startWithMerc(name)
	return campaign.startWithTeam{ name or "Barry" }
end

-- The first merc on the team, failing clearly if there is none.
function campaign.firstMerc()
	local m = ja2.state().mercs[1]
	ja2.expect(m, "the team has at least one merc")
	return m
end

-- --- Campaign state authoring (docs/plan/e2e-campaign-state.md) --------------
-- A test states a milestone instead of replaying the UI to get there:
--   campaign.stage{ day = 12, money = 120000, towns = { Drassen = { owned = true, loyalty = 70 } } }
-- ja2.debug("campaign", spec) mutates the live globals, so a save taken after
-- staging is a real save; ja2.campaign() reads the same state back.

local QUEST_STATUS = { [0] = "not_started", [1] = "in_progress", [2] = "done" }

-- Find a town in a ja2.campaign() state by name, case-insensitively (the state
-- keys towns by their internal name, e.g. "DRASSEN").
function campaign.town(state, name)
	local want = tostring(name):lower()
	for k, v in pairs(state.towns) do
		if k:lower() == want or tostring(v.name):lower() == want then return v end
	end
	return nil
end

-- Leave the laptop for the map screen (save/load and sector entry need it).
function campaign.toMap()
	ja2.waitIdle()
	if ja2.screen() == "LAPTOP_SCREEN" then
		ja2.click("Shut Down")
		ja2.waitScreen("MAP_SCREEN")
		campaign.dismissHelp()
	end
	return ja2.screen()
end

-- Apply a campaign state through ja2.debug("campaign") and return ja2.campaign().
-- With spec.save, also save the staged state under that name.
function campaign.stage(spec)
	ja2.debug("campaign", spec)
	ja2.step(2)
	if spec.save then
		campaign.toMap()
		ja2.save(spec.save)
	end
	return ja2.campaign()
end

-- Stage a state, then save and reload it, so every test starts from a real save
-- (the checkpoint helper). Returns the reloaded ja2.campaign().
function campaign.at(spec)
	campaign.stage(spec)
	campaign.toMap()
	local name = spec.save or "campaign-checkpoint"
	ja2.save(name)
	ja2.load(name)
	ja2.waitIdle()
	return ja2.campaign()
end

-- Assert a milestone against ja2.campaign(): any of day, money, difficulty,
-- mercs (a count), towns, sectors, quests and facts. Returns the state read.
function campaign.assertState(expected)
	local s = ja2.campaign()
	local function eq(got, want, what)
		ja2.expect(got == want, what .. ": expected " .. tostring(want) .. ", got " .. tostring(got))
	end
	if expected.day ~= nil then eq(s.day, expected.day, "day") end
	if expected.money ~= nil then eq(s.money, expected.money, "money") end
	if expected.difficulty ~= nil then eq(s.difficulty, expected.difficulty, "difficulty") end
	if expected.mercs ~= nil then eq(#s.mercs, expected.mercs, "merc count") end
	for name, want in pairs(expected.towns or {}) do
		local got = campaign.town(s, name)
		ja2.expect(got, "there is a town " .. name)
		if want.owned ~= nil then eq(got.owned, want.owned, name .. " owned") end
		if want.loyalty ~= nil then eq(got.loyalty, want.loyalty, name .. " loyalty") end
		for level, n in pairs(want.militia or {}) do
			eq(got.militia[level], n, name .. " " .. level .. " militia")
		end
	end
	for id, want in pairs(expected.sectors or {}) do
		local got = s.sectors[id]
		ja2.expect(got, "there is a sector " .. id)
		if want.enemy ~= nil then eq(got.enemy, want.enemy, id .. " enemy") end
		for _, k in ipairs({ "admins", "troops", "elites" }) do
			if want[k] ~= nil then eq(got[k], want[k], id .. " " .. k) end
		end
	end
	for name, want in pairs(expected.quests or {}) do
		local got = QUEST_STATUS[s.quests[name]] or s.quests[name]
		eq(got, want, "quest " .. name)
	end
	for name, want in pairs(expected.facts or {}) do
		eq(s.facts[name] == true, want == true, "fact " .. name)
	end
	return s
end

-- Load a sector in tactical for a world-map step (ja2.debug("entersector")).
-- spec is "A9" or { sector, npcs, clear_enemies }; returns ja2.state().
function campaign.enterSector(spec)
	if type(spec) == "string" then spec = { sector = spec } end
	ja2.expect(spec.sector, "enterSector needs a sector")
	campaign.toMap()
	ja2.debug("entersector", { sector = spec.sector, clear_enemies = spec.clear_enemies })
	ja2.waitScreen("GAME_SCREEN", 600000)
	campaign.dismissHelp()
	ja2.waitIdle()
	if spec.npcs then
		ja2.debug("npcs", spec.npcs)
		ja2.waitIdle()
	end
	return ja2.state()
end

-- Take a player-controlled town and walk into one of its sectors, with townsfolk.
-- Returns ja2.state().
function campaign.enterTown(name, opts)
	opts = opts or {}
	ja2.debug("campaign", { towns = { [name] = { owned = true, loyalty = opts.loyalty or 60 } } })
	ja2.step(2)
	local t = campaign.town(ja2.campaign(), name)
	ja2.expect(t, "there is a town " .. name)
	return campaign.enterSector{
		sector = opts.sector or t.sectors[1],
		npcs = opts.npcs == nil and 3 or opts.npcs,
	}
end

-- Walk into a sector and stage a fight there (ja2.debug("entersector") then the
-- battle harness). spec is a ja2.debug("battle") spec plus `sector`; returns the
-- tactical state.
function campaign.stepIntoBattle(spec)
	spec = spec or {}
	campaign.enterSector{ sector = spec.sector, clear_enemies = true }
	local battle = require("lib.battle")
	local b = {}
	for k, v in pairs(spec) do b[k] = v end
	b.sector = nil
	return battle.stage(b)
end

return campaign
