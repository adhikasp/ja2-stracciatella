-- M1 audit of the legacy laptop (docs/ui/laptop.md): walks every program and web site of a new campaign, with
-- every bookmark set (ja2.debug("bookmarks")), and logs the visible text and the clickable elements of each state
-- next to a screenshot. Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/phase6_audit.lua --isolated --out <dir>
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")

-- some pages never count as idle (a blinking caret, a talking head), so settle with a bounded wait
local function settle() ja2.wait(1500); pcall(ja2.waitIdle, 3000) end

local function dump(name)
	settle()
	ja2.screenshot(name .. ".png")
	local lines = {}
	for _, t in ipairs(ja2.texts()) do lines[#lines + 1] = string.format("%d,%d %s", t.x, t.y, t.text) end
	ja2.log("== TEXT " .. name .. "\n" .. table.concat(lines, "\n"))
	local ui = {}
	for _, u in ipairs(ja2.ui()) do
		ui[#ui + 1] = string.format("%s %d,%d %dx%d [%s]%s", u.kind, u.x, u.y, u.w, u.h, u.label or "", u.enabled and "" or " (disabled)")
	end
	ja2.log("== UI " .. name .. "\n" .. table.concat(ui, "\n"))
end

local function try(what, fn)
	local ok, err = pcall(fn)
	if not ok then ja2.log("!! " .. what .. ": " .. tostring(err)) end
	return ok
end

local function click(loc) ja2.click(loc); settle() end

local function web(site)
	ja2.click{text = "Web", exact = true, within = campaign.std{x = 0, y = 0, w = 110, h = 480}}
	settle()
	if not ja2.exists{text = site, exact = true} then
		ja2.click{text = "Web", exact = true, within = campaign.std{x = 0, y = 0, w = 110, h = 480}}
		settle()
	end
	click{text = site, exact = true}
	ja2.wait(4000)
	pcall(campaign.dismissHelp)
end

ja2.setVideo{ res = "1280x720" }
campaign.newGame()
ja2.debug("bookmarks")
dump("00_desktop_email")

try("email", function()
	ja2.click{text = "E-mail", exact = true, within = campaign.std{x = 0, y = 0, w = 110, h = 480}}
	settle()
	dump("00b_email")
	click("Inquiry")
	dump("01_email_read")
	click("Close message")
end)

for _, p in ipairs({ { "Files", "02_files" }, { "History", "03_history" }, { "Personnel", "04_personnel" },
	{ "Financial", "05_finances" } }) do
	try(p[1], function()
		ja2.click{text = p[1], exact = true, within = campaign.std{x = 0, y = 0, w = 110, h = 480}}
		ja2.waitIdle()
		dump(p[2])
	end)
end

try("bookmarks", function()
	click("Web")
	dump("10_web_bookmarks")
end)

try("aim", function()
	click("A.I.M.")
	dump("11_aim_home")
	click{text = "Members", exact = true, within = campaign.std{x = 200, y = 200, w = 350, h = 150}}
	dump("12_aim_sort")
	click("mug shot index")
	dump("13_aim_facial_index")
	click{text = "Barry", exact = true}
	dump("14_aim_member")
	click("Contact")
	ja2.waitFor("HIRE")
	dump("15_aim_contact")
	click("HIRE")
	dump("16_aim_hire")
	click("One Week")
	click("No Equipment")
	dump("17_aim_hire_selected")
	click("TRANSFER FUNDS")
	ja2.waitFor("TRANSFER SUCCESSFUL")
	dump("18_aim_transfer")
	click{text = "OK", exact = true}
	campaign.acceptMessageBox("should arrive")
	dump("19_aim_after_hire")
end)

try("aim subpages", function()
	web("A.I.M.")
	click("Policies"); dump("20_aim_policies"); click("Disagree")
	click("History"); dump("21_aim_history")
	click("A.I.M."); click("Links"); dump("22_aim_links")
end)

try("aim archives", function()
	web("A.I.M.")
	click{text = "Members", exact = true, within = campaign.std{x = 200, y = 200, w = 350, h = 150}}
	click("Alumni")
	dump("23_aim_alumni")
end)

try("merc", function() web("M.E.R.C."); dump("30_merc_home") end)
try("merc files", function() click{text = "Files", within = campaign.std{x = 110, y = 0, w = 530, h = 480}}; dump("31_merc_files") end)
try("merc account", function() web("M.E.R.C."); click("Account"); dump("32_merc_account") end)

try("imp", function() web("I.M.P"); dump("40_imp_home") end)
try("imp about", function() click("About Us"); dump("41_imp_about"); click("Back") end)

try("bobby", function() web("Bobby Ray's"); dump("50_bobby_home") end)
try("bobby guns", function() click("Guns"); dump("51_bobby_guns") end)
try("bobby order", function()
	click("Order Form")
	dump("52_bobby_order")
end)

try("florist", function() web("Florist"); dump("60_florist") end)
try("florist gallery", function() click("Gallery"); dump("61_florist_gallery") end)
try("insurance", function() web("Insurance"); dump("70_insurance") end)
try("funeral", function() web("Mortuary"); dump("80_funeral") end)

try("personnel after hire", function()
	ja2.click{text = "Personnel", exact = true, within = campaign.std{x = 0, y = 0, w = 110, h = 480}}
	ja2.waitIdle()
	dump("90_personnel_hired")
end)
try("finances after hire", function()
	ja2.click{text = "Financial", exact = true, within = campaign.std{x = 0, y = 0, w = 110, h = 480}}
	ja2.waitIdle()
	dump("91_finances_hired")
end)
