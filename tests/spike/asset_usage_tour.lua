-- Asset usage map (Phase 0 asset tooling): walk the main screens of a new campaign and record which images
-- each screen loads (ja2.recordImageUsage). tools/assets/usage.py turns image_usage.jsonl into usage.json for
-- the manifest. Images already cached when a screen is entered count for the screen that loaded them first.
local here = debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "./"
package.path = here .. "../e2e/?.lua;" .. package.path
local campaign = require("lib.campaign")

ja2.recordImageUsage("image_usage.jsonl")
ja2.waitScreen("MAINMENU_SCREEN")

ja2.click("Preferences")
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{text = "Done", exact = true}
ja2.waitScreen("MAINMENU_SCREEN")
ja2.click("Credits")
ja2.waitScreen("CREDIT_SCREEN")
ja2.wait(3000)
ja2.key("ESC")
ja2.waitScreen("MAINMENU_SCREEN")

-- Laptop: every program, A.I.M., then hire and land
campaign.newGame()
local function open(program)
	if campaign.nativeLaptop() then
		local ids = { Files = "files", History = "history", Personnel = "personnel", Financial = "finances", ["E-mail"] = "email" }
		ja2.click{id = "laptop.app." .. ids[program]}
		ja2.waitIdle()
		return
	end
	ja2.click{text = program, exact = true, within = campaign.std{x = 0, y = 0, w = 110, h = 480}}
	ja2.waitIdle()
end
for _, p in ipairs({ "Files", "History", "Personnel", "Financial", "E-mail" }) do open(p) end
campaign.hireFromAim("Barry")
campaign.landInArulco()

-- Tactical extras
ja2.debug("message", "usage tour")
ja2.debug("loadscreen", 1)
ja2.waitIdle()

local usage = ja2.imageUsage()
local n, screens = 0, 0
for screen, files in pairs(usage) do
	screens = screens + 1
	n = n + #files
	ja2.log(string.format("usage: %s loaded %d images", screen, #files))
end
ja2.expect(screens >= 4 and n > 100, string.format("images recorded on several screens (%d on %d)", n, screens))
