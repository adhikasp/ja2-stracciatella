-- A campaign for the strategic map tests (docs/ui/mapscreen.md): Barry and Ivan (with his gear) landed in Omerta (A9), the sector
-- cleared with the test aid ja2.debug("clearenemies"), on the map screen. Made once per home directory and kept as the
-- save "mapcampaign", so later runs only load it. Load with: local mapcampaign = require("lib.mapcampaign")
local campaign = require("lib.campaign")

local mapcampaign = {}

local function has(name)
	for _, s in ipairs(ja2.saves()) do if s == name then return true end end
	return false
end

-- To the map screen with the campaign above; returns ja2.state().
function mapcampaign.start()
	if has("mapcampaign") then
		ja2.load("mapcampaign")
	else
		campaign.newGame()
		campaign.hireFromAim("Barry")
		-- the map tests only need a stable campaign; the default one-week contract matches the
		-- legacy hire (the legacy A.I.M. page silently keeps its default length)
		campaign.hireFromAim("Ivan", "One Week", true)
		ja2.click("Shut Down")
		ja2.waitScreen("MAP_SCREEN")
		campaign.dismissHelp()
		ja2.click("Time Compress (+)")
		ja2.waitScreen("GAME_SCREEN", 600000)
		campaign.dismissHelp()
		ja2.debug("clearenemies")
		ja2.waitIdle()
		ja2.key("m")
		ja2.waitScreen("MAP_SCREEN")
		campaign.dismissHelp()
		ja2.save("mapcampaign", "Map screen tests")
	end
	if ja2.screen() ~= "MAP_SCREEN" then
		ja2.key("m")
		ja2.waitScreen("MAP_SCREEN")
	end
	campaign.dismissHelp()
	return ja2.state()
end

return mapcampaign
