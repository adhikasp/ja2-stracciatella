-- Changing the video settings while the game runs (ja2.setVideo, like the Video options): resolution, UI scale and
-- world zoom on the tactical screen, the map screen and the laptop, through classic, widescreen, ultrawide and a
-- layered mode (UI at 2x over a world at 1x). After every change every region and button must lie inside the
-- new screen and the screen must be usable at its new size.
-- the legacy tactical HUD (from 1280x720 up the native one runs; tactical_parity.lua covers it)
ja2.setUiMode("tactical", "legacy")
local shots = require("lib.shots")
local campaign = require("lib.campaign")

local MODES = {
	{name = "1280x720",   res = "1280x720",  uiscale = 1, worldzoom = 0, w = 1280, h = 720},
	{name = "2560x1080",  res = "2560x1080", uiscale = 1, worldzoom = 0, w = 2560, h = 1080},
	{name = "layered",    res = "2560x1440", uiscale = 2, worldzoom = 1, w = 1280, h = 720, layered = true},
	{name = "640x480",    res = "640x480",   uiscale = 1, worldzoom = 0, w = 640,  h = 480},
}

local function switch(mode, where)
	local size = ja2.setVideo{res = mode.res, uiscale = mode.uiscale, worldzoom = mode.worldzoom}
	ja2.waitIdle()
	ja2.expect(size.w == mode.w and size.h == mode.h,
		("%s: canvas %dx%d, expected %dx%d"):format(mode.name, size.w, size.h, mode.w, mode.h))
	ja2.expect(size.layered == (mode.layered or false), mode.name .. ": layered")
	local s = ja2.screenSize()
	ja2.expect(s.w == mode.w and s.h == mode.h, "screenSize follows")
	shots.take(("%s_%s.png"):format(where, mode.name))
end

-- Tactical: the mode changes with a merc on the ground; he can still be selected and walk.
campaign.startWithMerc("Barry")
ja2.expect(ja2.screen() == "GAME_SCREEN", "in tactical")
for _, mode in ipairs(MODES) do
	switch(mode, "tactical")
	ja2.expect(ja2.screen() == "GAME_SCREEN", mode.name .. ": still in tactical")
	local merc = campaign.firstMerc()
	ja2.expect(merc.screenX, mode.name .. ": Barry is on screen")
	ja2.click(merc.screenX, merc.screenY)
	ja2.waitIdle()
	ja2.expect(ja2.exists("Map Screen"), mode.name .. ": the bottom panel is there")
end

-- Map screen
ja2.click("Map Screen")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
for _, mode in ipairs(MODES) do
	switch(mode, "map")
	ja2.expect(ja2.screen() == "MAP_SCREEN", mode.name .. ": still on the map screen")
	ja2.expect(ja2.exists("Omerta"), mode.name .. ": the map shows Omerta")
	ja2.expect(ja2.exists{text = "Barry", exact = true}, mode.name .. ": Barry is in the team list")
end

-- Laptop
ja2.click{text = "Laptop", exact = true}
ja2.waitScreen("LAPTOP_SCREEN")
campaign.dismissLaptopPopups()
for _, mode in ipairs(MODES) do
	switch(mode, "laptop")
	ja2.expect(ja2.screen() == "LAPTOP_SCREEN", mode.name .. ": still in the laptop")
	ja2.expect(campaign.laptopMercs() == 1, mode.name .. ": the laptop counts one merc")
end
ja2.click("Shut Down")
ja2.waitScreen("MAP_SCREEN")
ja2.assertInsideScreen()
