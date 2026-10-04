-- The strategic map with a merc on the ground: the clock runs and pauses,
-- the merc's inventory opens, and Options and the laptop are reachable.
-- the legacy tactical HUD (from 1280x720 up the native one runs; tactical_parity.lua covers it)
ja2.setUiMode("tactical", "legacy")
local shots = require("lib.shots")
local campaign = require("lib.campaign")

-- This is the tour of the legacy map screen (the native one has tests/e2e/mapscreen_parity.lua).
ja2.setUiMode("mapscreen", "legacy")

campaign.startWithMerc("Barry")
ja2.click("Map Screen")
ja2.waitScreen("MAP_SCREEN")
campaign.dismissHelp()
ja2.expect(ja2.exists("Omerta"), "the map shows Omerta")
ja2.expect(ja2.exists{text = "Barry", exact = true}, "Barry is in the team list")

-- Omerta is enemy-held at the start, so time compression is unavailable;
-- the Pause key still pauses and resumes the game.
ja2.expect(ja2.state().tactical.enemyInSector, "Omerta starts out enemy-held")
local paused = ja2.state().time.paused
ja2.key("pause")
ja2.waitIdle()
ja2.expect(ja2.state().time.paused ~= paused, "Pause toggles the pause")
ja2.key("pause")
ja2.waitIdle()
ja2.expect(ja2.state().time.paused == paused, "and toggles it back")

-- Barry's inventory.
ja2.click("Enter Inventory")
ja2.waitIdle()
shots.take("inventory.png")
ja2.key("ESC")
ja2.waitIdle()

-- Popups of the character list: assignment and contract. They open next to the list, not in the middle of the screen.
local col = {x = 0, y = 107, w = 261, h = 252} -- the column is in the top-left corner
ja2.click{text = "Squad 1", exact = true, within = col}
ja2.waitIdle()
ja2.expect(ja2.exists("Doctor"), "the assignment menu opens")
shots.take("assignment.png", true)
ja2.key("ESC")
ja2.waitIdle()
ja2.click{text = "Contract", exact = true}
ja2.waitIdle()
ja2.expect(ja2.exists("Dismiss"), "the contract menu opens")
shots.take("contract.png", true)
ja2.key("ESC")
ja2.waitIdle()

-- The sector inventory covers the map.
ja2.key("ctrl+i")
ja2.waitIdle()
shots.take("sector_inventory.png", true)
ja2.key("ctrl+i")
ja2.waitIdle()

-- Options and back.
ja2.click{text = "Options", exact = true}
ja2.waitScreen("OPTIONS_SCREEN")
ja2.click{text = "Done", exact = true}
ja2.waitScreen("MAP_SCREEN")

-- The laptop and back.
ja2.click{text = "Laptop", exact = true}
ja2.waitScreen("LAPTOP_SCREEN")
campaign.dismissLaptopPopups()
ja2.expect(campaign.laptopMercs() == 1, "the laptop counts one merc")
ja2.click("Shut Down")
ja2.waitScreen("MAP_SCREEN")
shots.take("map.png", true)
