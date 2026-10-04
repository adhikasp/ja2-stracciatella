-- Screenshots of the Phase 6 laptop wireframes (M2; assets/ui/mocks/phase6/*.rml, written by tools/ui/phase6_mocks.py)
-- through the native runtime at several output sizes, with the layout audit logged. Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/phase6_mocks.lua --isolated --out <dir> [--arg desktop,email_read] [--arg 1920x1080]
local only = ja2.args and ja2.args[1] ~= "" and ja2.args[1] or nil
local mocks = { "desktop", "email_inbox", "email_read", "aim_members", "aim_member", "merc", "imp_quiz", "bobby_shop",
	"bobby_order", "finances", "personnel" }
local sizes = { "1920x1080", "3440x1440", "1280x720" }
if ja2.args and ja2.args[2] then sizes = { ja2.args[2] } end

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
for _, size in ipairs(sizes) do
	ja2.setVideo{ res = size }
	ja2.waitIdle()
	for _, m in ipairs(mocks) do
		if not only or only == "all" or only:find(m, 1, true) then
			ja2.debug("mock", "phase6/" .. m)
			ja2.wait(300)
			ja2.move(ja2.screenSize().w - 3, ja2.screenSize().h - 3)
			ja2.wait(200)
			ja2.screenshot(m .. "_" .. size .. ".png")
			for _, p in ipairs(ja2.layoutProblems()) do ja2.log(m .. " " .. size .. ": " .. p) end
			ja2.log(m .. " " .. size .. " warnings: " .. tostring(ja2.nativeUi().warnings))
			ja2.key("ESC")
			ja2.wait(200)
		end
	end
end
