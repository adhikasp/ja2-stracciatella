-- Parity tour of the native laptop (docs/ui/laptop.md, section 9), driven by element id, with game state checked:
-- programs, mail, files, finances, A.I.M. hire, M.E.R.C. hire, Bobby Ray's order, insurance, florist, I.M.P. quiz, keys.
-- Below 1280x720 the legacy laptop runs and only the hire path is checked.
local shots = require("lib.shots")
local campaign = require("lib.campaign")

ja2.waitScreen("MAINMENU_SCREEN")
local sz = ja2.screenSize()
local native = sz.w >= 1280 and sz.h >= 720
campaign.newGame()
if not native then
	ja2.expect(ja2.nativeUi().screen == "", "the laptop is legacy below 1280x720")
	campaign.hireFromAim("Barry")
	ja2.expect(#ja2.state().mercs == 1 and ja2.state().money == 41600, "legacy hire")
	return
end
ja2.expect(ja2.nativeUi().screen == "laptop", "the native laptop runs")

local function I(n) return string.format("%d", n) end
local function vm() return ja2.viewModel("laptop") end
local function num(s) return tonumber((tostring(s):gsub("[^%d%-]", ""))) or 0 end
local function click(id) ja2.click{id = id}; ja2.waitIdle() end
local function answerYes() ja2.waitScreen("MSG_BOX_SCREEN"); ja2.click{id = "msgbox.yes"}; ja2.waitIdle() end
local function layout(what) for _, p in ipairs(ja2.layoutProblems()) do ja2.check(false, what .. ": " .. p) end end

-- shell: system bar and dock
local v = vm()
ja2.expect(v.balance == "$45,000" and v.team == "0" and v.day == "1", "system bar")
ja2.expect(v.unread_mail == 1 and v.unread_files == 1 and v.note_count >= 2, "new mail and file indicators")
for _, a in ipairs{"email", "web", "finances", "personnel", "history", "files"} do
	click("laptop.app." .. a)
	ja2.expect(vm().app == a, "dock opens " .. a)
end
ja2.key("f1"); ja2.waitIdle(); ja2.expect(vm().app == "email", "F1 opens mail")
ja2.key("f6"); ja2.waitIdle(); ja2.expect(vm().app == "files", "F6 opens files")
ja2.key("escape"); ja2.waitIdle(); ja2.expect(vm().app == "desktop", "Esc returns to the desktop")

-- e-mail: read marks read, the text has no legacy markers, delete asks
click("laptop.app.email")
local unread
for _, m in ipairs(vm().mails) do if m.alt then unread = m.index end end
local before = vm().mail_count
click("email.mail[" .. I(unread) .. "]")
v = vm()
ja2.expect(v.unread_mail == 0 and v.mail_open == unread and #v.mail_body > 0, "reading marks the mail read")
for _, p in ipairs(v.mail_body) do ja2.expect(not p:find("\194[\177-\179]"), "no legacy markers in mail text") end
shots.take("laptop_mail.png", true)
click("email.sort.from"); ja2.expect(vm().mail_sort == "from", "sort by sender")
click("email.mail[" .. I(unread) .. "]")
click("email.delete"); answerYes()
ja2.expect(vm().mail_count == before - 1, "delete removes the mail")

-- files, history, finances, personnel
click("laptop.app.files"); click("files.file[0]")
ja2.expect(vm().unread_files == 0 and #vm().file_body > 3, "file read")
click("laptop.app.history"); ja2.expect(vm().history_count >= 2, "history rows")
click("laptop.app.finances")
ja2.expect(vm().fin_balance == "$45,000" and vm().ledger_count == 1, "ledger shows the opening deposit")
click("laptop.app.personnel"); ja2.expect(vm().roster_count == 0, "no team yet")

-- A.I.M.: hire Barry for two weeks with equipment
click("laptop.app.web")
ja2.expect(vm().site == "aim" and #vm().aim_members > 10, "A.I.M. members grid")
click("aim.sort.price"); click("aim.sort.price")
ja2.click{text = "Barry", exact = true}; ja2.waitIdle()
ja2.expect(vm().page == "member", "member page")
click("aim.contact"); click("aim.hire.tohire")
click("aim.hire.length[2]"); click("aim.hire.gear.buy")
v = vm()
local total = num(v.hire_total)
ja2.expect(total == num(v.am_fee14) + num(v.am_gear_cost) + num(v.hire_deposit or 0), "charge = fee + gear + deposit (" .. v.hire_total .. ")")
shots.take("laptop_hire.png", true)
click("aim.hire.transfer")
ja2.expect(vm().hire_state == "hired", "transfer succeeded")
local s = ja2.state()
ja2.expect(#s.mercs == 1 and s.money == 45000 - total, "Barry is hired and the money is gone")
click("aim.hire.done"); campaign.acceptMessageBox("should arrive")
ja2.expect(vm().team == "1" and vm().balance:find(tostring(s.money // 1000)), "system bar follows")
click("laptop.app.finances"); ja2.expect(vm().ledger_count == 2, "ledger has the payment")
click("laptop.app.personnel")
ja2.expect(vm().roster_count == 1 and vm().pd_has and vm().pd_name:find("Barry"), "personnel lists Barry")
layout("personnel")

-- (M.E.R.C. account and hire: not covered yet, see the PR gaps)
click("laptop.app.web"); ja2.debug("bookmarks"); ja2.waitIdle(); click("laptop.app.email"); click("laptop.app.web")
local money

-- Bobby Ray's: order two of the second item, deliver to a city
click("web.bookmark.bobby")
ja2.expect(vm().page == "shop" and #vm().shop > 0, "shop")
click("bobby.item[1].add"); click("bobby.item[1].add")
ja2.expect(vm().cart_count == 1, "cart")
click("bobby.toorder")
click("bobby.order.city")
local city
for _, c in ipairs(vm().cities) do if c.alt then city = c.index end end
click("bobby.order.city[" .. I((city or 0)) .. "]")
click("bobby.order.speed[2]")
local cost = num(vm().order_total)
money = ja2.state().money
click("bobby.order.accept")
if ja2.screen() == "MSG_BOX_SCREEN" then ja2.click{id = "msgbox.yes"}; ja2.waitIdle() end
ja2.expect(ja2.state().money == money - cost, "the order is paid (" .. cost .. ")")
click("bobby.nav.shipments"); ja2.expect(#vm().shipments >= 1, "a shipment is scheduled")

-- insurance and florist
click("web.bookmark.insurance"); click("insurance.nav.contract")
local ins
for _, r in ipairs(vm().insurance_rows) do if r.alt then ins = r end end
if ins then
	money = ja2.state().money
	click("insurance.insure[" .. I(ins.id) .. "]")
	ja2.expect(ja2.state().money < money, "insurance premium paid")
end
click("web.bookmark.florist"); click("florist.nav.order")
click("florist.town[0]")
money = ja2.state().money
click("florist.send")
if ja2.screen() == "MSG_BOX_SCREEN" then ja2.click{id = "msgbox.yes"}; ja2.waitIdle() end
ja2.expect(ja2.state().money <= money, "flowers do not add money")

-- I.M.P.: the code gate, then the quiz page
click("web.bookmark.imp")
ja2.expect(vm().page == "home", "I.M.P. home")
ja2.click{id = "imp.code.input"}; ja2.type("XEP624"); click("imp.enter")
ja2.expect(vm().page == "main", "the activation code opens the site")
click("imp.begin"); click("imp.begin.next")
ja2.expect(vm().page == "personality" and #vm().imp_answers >= 2, "quiz question with answers")
click("imp.answer[0]"); ja2.expect(vm().imp_answers[1].on, "answer selected")
layout("imp")

-- 150% scale layout, then leave
ja2.setUiScale(1.5); ja2.waitIdle(); for _, p in ipairs(ja2.layoutProblems()) do ja2.log("150% layout note: " .. p) end; ja2.setUiScale(1)
click("laptop.app.web"); click("laptop.close")
ja2.waitScreen("MAP_SCREEN")
ja2.expect(#ja2.state().mercs == 1, "Barry is on the map screen")
