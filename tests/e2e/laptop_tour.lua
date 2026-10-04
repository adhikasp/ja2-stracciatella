-- Every laptop program of a new campaign opens and shows its content.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

campaign.newGame()

-- The native laptop (1280x720 and larger): every program opens by id; laptop_parity.lua goes deeper.
if campaign.nativeLaptop() then
	for _, a in ipairs{"files", "history", "personnel", "finances", "email", "web"} do
		ja2.click{id = "laptop.app." .. a}
		ja2.waitIdle()
		ja2.expect(ja2.viewModel("laptop").app == a, a .. " opens")
	end
	ja2.expect(ja2.viewModel("laptop").balance == "$45,000", "the balance is $45,000")
	shots.take("laptop.png", true)
	ja2.click{id = "laptop.close"}
	ja2.waitScreen("MAP_SCREEN")
	ja2.expect(#ja2.state().mercs == 0 and ja2.state().money == 45000, "no mercs hired and no money spent")
	return
end

local function open(program, expected)
	ja2.click{text = program, exact = true, within = campaign.std{x = 0, y = 0, w = 110, h = 480}}
	ja2.waitIdle()
	ja2.expect(ja2.exists(expected), program .. " shows \"" .. expected .. "\"")
end

open("Files", "File Viewer")
ja2.expect(ja2.exists("Recon Report"), "the R.I.S. recon report is on file")

open("History", "History Log")
ja2.expect(ja2.exists("Accepted Assignment From Enrico"), "the history starts with Enrico's assignment")

open("Personnel", "Personnel Manager")
ja2.expect(ja2.exists("Current Team ( 0 )"), "nobody is hired yet")

open("Financial", "Bookkeeper Plus")
ja2.expect(ja2.exists("$45,000"), "the balance is $45,000")

-- Read the first mail and close it again.
open("E-mail", "Mail Box")
ja2.click("Inquiry")
ja2.waitIdle()
ja2.expect(ja2.exists("Enrico Chivaldori"), "the mail is signed by Enrico")
ja2.click("Close message")
ja2.waitIdle()
ja2.expect(not ja2.exists("Enrico Chivaldori"), "the message closed")

-- The web browser offers A.I.M. to begin with.
open("Web", "A.I.M.")
ja2.click("A.I.M.")
ja2.waitIdle()
ja2.expect(ja2.exists("ASSOCIATION OF INTERNATIONAL MERCENARIES"), "the A.I.M. home page loads")
-- The policies page must be agreed to before reading on; decline instead.
ja2.click("Policies")
ja2.waitIdle()
ja2.expect(ja2.exists("Agree"), "the policies page asks for agreement")
ja2.click("Disagree")
ja2.waitIdle()
ja2.expect(ja2.exists("Members"), "declining returns to the A.I.M. home page")
ja2.click("Close program")
ja2.waitIdle()
shots.take("laptop.png", true)

-- Shutting down leads to the strategic map, with nothing spent yet.
ja2.click("Shut Down")
ja2.waitScreen("MAP_SCREEN")
local s = ja2.state()
ja2.expect(#s.mercs == 0 and s.money == 45000, "no mercs hired and no money spent")
ja2.expect(s.time.day == 1 and s.time.hour == 1, "it is still day 1, 01:00")
