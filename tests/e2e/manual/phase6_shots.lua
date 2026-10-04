-- Screenshots of the native laptop (docs/ui/laptop.md) in a new campaign with every bookmark set, at the size given
-- (default 1920x1080) and the UI scale given (default 1). Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/phase6_shots.lua --isolated --out <dir> [--arg 1920x1080] [--arg 1.5]
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")
local size = ja2.args and ja2.args[1] or "1920x1080"
local scale = ja2.args and tonumber(ja2.args[2]) or 1
local suffix = "_" .. size .. (scale ~= 1 and ("_" .. tostring(scale * 100) .. "pct") or "")

ja2.setVideo{ res = size }
ja2.setUiScale(scale)
campaign.newGame()
ja2.wait(6000)
ja2.debug("bookmarks")
ja2.waitIdle()

local function shot(name)
	ja2.wait(300)
	ja2.move(ja2.screenSize().w - 3, ja2.screenSize().h - 3)
	ja2.wait(100)
	ja2.screenshot(name .. suffix .. ".png")
	for _, p in ipairs(ja2.layoutProblems()) do ja2.log(name .. ": " .. p) end
end
local function click(id) ja2.click{id = id}; ja2.wait(200) end

shot("desktop")
click("laptop.app.email"); shot("email_inbox")
click("email.mail[0]"); shot("email_read")
click("laptop.app.files"); click("files.file[0]"); shot("files")
click("laptop.app.history"); shot("history")
click("laptop.app.finances"); shot("finances")
click("laptop.app.web"); shot("aim_members")
click("aim.member[0]"); shot("aim_member")
click("aim.contact"); click("aim.hire.tohire"); shot("aim_hire")
click("aim.hire.length[1]"); click("aim.hire.transfer"); shot("aim_hired")
click("aim.hire.done")
if ja2.state().messageBox then ja2.click{text = "OK", exact = true}; ja2.wait(300) end
click("aim.nav.policies"); shot("aim_policies")
click("web.bookmark.merc"); shot("merc")
click("web.bookmark.bobby"); shot("bobby_shop")
click("bobby.item[1].add"); click("bobby.item[1].add"); shot("bobby_cart")
click("bobby.toorder"); click("bobby.order.city"); shot("bobby_cities")
ja2.click{text = "Drassen", within = {x = 0, y = 0, w = ja2.screenSize().w, h = ja2.screenSize().h}}; ja2.wait(200)
shot("bobby_order")
click("web.bookmark.imp"); shot("imp_home")
click("web.bookmark.florist"); shot("florist")
click("web.bookmark.insurance"); click("insurance.nav.contract"); shot("insurance")
click("web.bookmark.funeral"); shot("funeral")
click("laptop.app.personnel"); shot("personnel")
click("laptop.app.finances"); shot("finances_after")
