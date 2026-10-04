// The native laptop (Phase 6, docs/ui/laptop.md): a full-screen "sir-FER" OS with a dock of programs (e-mail, web,
// finances, personnel, history, files) and a browser whose sites each keep their own identity. Everything shown is
// read from the game (LaptopNative.h, the profiles, the soldiers); every action calls the function the legacy page
// calls, so a hire really hires, an order really ships and money really changes.
#include "LaptopViewModel.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"

#include "AIM.h"
#include "AIMMembers.h"
#include "Cursor_Control.h"
#include "Dialogue_Control.h"
#include "EMail.h"
#include "ContentManager.h"
#include "Finances.h"
#include "GameInstance.h"
#include "ItemModel.h"
#include "Map_Screen_Interface.h"
#include "Merc_Contract.h"
#include "Game_Clock.h"
#include "Input.h"
#include "Laptop.h"
#include "LaptopNative.h"
#include "LaptopSave.h"
#include "MessageBoxScreen.h"
#include "Overhead.h"
#include "Soldier_Profile.h"
#include "StrategicMap.h"
#include "Text.h"
#include "Video.h"

#include <string_theory/format>

#include <algorithm>

namespace NativeUI
{

namespace
{
	std::string S(ST::string const& s) { return s.to_std_string(); }
	/** Mail and file records carry the legacy font markers (U+00B1 to U+00B3: line end, key, title); the page flows its own text. */
	std::string Clean(ST::string const& in)
	{
		std::string out;
		std::string const s = in.to_std_string();
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == '\xC2' && i + 1 < s.size() && (s[i + 1] == '\xB1' || s[i + 1] == '\xB2' || s[i + 1] == '\xB3')) { out += ' '; ++i; continue; }
			out += s[i];
		}
		// collapse the blanks the markers leave and trim
		std::string r;
		for (char const c : out) { if (c == ' ' && (r.empty() || r.back() == ' ')) continue; r += c; }
		while (!r.empty() && r.back() == ' ') r.pop_back();
		return r;
	}
	std::string Money(INT32 const v) { return S(SPrintMoney(v)); }
	std::string Day(UINT32 const minutes) { return std::to_string(minutes / (24 * 60)); }

	struct SiteDef { int bookmark; char const* id; char const* cls; char const* host; int mode; };
	SiteDef const g_sites[] = {
		{ AIM_BOOKMARK,       "aim",       "bm-aim",     "www.aim.com",           LAPTOP_MODE_AIM },
		{ BOBBYR_BOOKMARK,    "bobby",     "bm-bobby",   "www.bobbyrays.com",     LAPTOP_MODE_BOBBY_R },
		{ IMP_BOOKMARK,       "imp",       "bm-imp",     "www.imp.com",           LAPTOP_MODE_CHAR_PROFILE },
		{ MERC_BOOKMARK,      "merc",      "bm-merc",    "www.speck.com",         LAPTOP_MODE_MERC },
		{ FUNERAL_BOOKMARK,   "funeral",   "bm-fun",     "www.mcgillicuttys.com", LAPTOP_MODE_FUNERAL },
		{ FLORIST_BOOKMARK,   "florist",   "bm-florist", "www.florist.com",       LAPTOP_MODE_FLORIST },
		{ INSURANCE_BOOKMARK, "insurance", "bm-ins",     "www.mis-insurance.com", LAPTOP_MODE_INSURANCE },
	};
	SiteDef const* FindSite(std::string const& id)
	{
		for (SiteDef const& s : g_sites) if (id == s.id) return &s;
		return nullptr;
	}

	// the indices of pPersonnelScreenStrings (Personnel.cc)
	enum { PRSNL_TXT_MED_DEPOSIT, PRSNL_TXT_CURRENT_CONTRACT, PRSNL_TXT_KILLS, PRSNL_TXT_ASSISTS, PRSNL_TXT_DAILY_COST,
		PRSNL_TXT_TOTAL_COST, PRSNL_TXT_CONTRACT, PRSNL_TXT_TOTAL_SERVICE, PRSNL_TXT_UNPAID_AMOUNT, PRSNL_TXT_HIT_PERCENTAGE,
		PRSNL_TXT_BATTLES, PRSNL_TXT_TIMES_WOUNDED };

	std::string Pct(int v, int max = 100) { return std::to_string(std::clamp(max ? v * 100 / max : 0, 0, 100)) + "%"; }
}


LaptopViewModel::LaptopViewModel() : ViewModel("laptop", TOPIC_MONEY | TOPIC_TEAM | TOPIC_CLOCK)
{
	Command("app", [this](Args const& a) { if (!a.empty()) OpenApp(a[0]); });
	Command("site", [this](Args const& a) { if (!a.empty()) OpenSite(a[0]); });
	Command("page", [this](Args const& a) { if (!a.empty()) GoPage(a[0]); });
	Command("back", [this](Args const&) { History(-1); });
	Command("forward", [this](Args const&) { History(+1); });
	Command("close", [this](Args const&) { if (onAction) onAction("close"); });
	Command("help", [this](Args const&) { if (onAction) onAction("help"); });

	// e-mail
	Command("mail_open", [this](Args const& a) { if (!a.empty()) MailOpen(std::atoi(a[0].c_str())); });
	Command("mail_close", [this](Args const&) { mailOpen = -1; ReadMail(); });
	Command("mail_delete", [this](Args const& a) {
		int const i = a.empty() ? mailOpen : std::atoi(a[0].c_str());
		if (i >= 0 && onAsk) onAsk("mail_delete", std::to_string(i));
	});
	Command("mail_step", [this](Args const& a) { if (!a.empty()) MailStep(std::atoi(a[0].c_str())); });
	Command("mail_sort", [this](Args const& a) {
		if (a.empty()) return;
		if (mailSort == a[0]) mailSortDesc = !mailSortDesc; else { mailSort = a[0]; mailSortDesc = a[0] == "day"; }
		ReadMail();
	});

	// files
	Command("file_open", [this](Args const& a) { if (!a.empty()) FileOpen(std::atoi(a[0].c_str())); });

	// personnel
	Command("merc_select", [this](Args const& a) { if (!a.empty()) { persSelected = std::atoi(a[0].c_str()); ReadPersonnel(); } });
	Command("pers_tab", [this](Args const& a) { if (!a.empty()) { persTab = a[0]; persSelected = 0; ReadPersonnel(); } });
	Command("pers_view", [this](Args const& a) { if (!a.empty()) { persView = a[0]; Changed(); } });

	// A.I.M.
	Command("aim_sort", [this](Args const& a) {
		if (a.empty()) return;
		if (aimSort == a[0]) aimSortDesc = !aimSortDesc; else { aimSort = a[0]; aimSortDesc = false; }
		ReadAim();
	});
	Command("aim_filter", [this](Args const& a) { aimAvailableOnly = !a.empty() && a[0] == "available"; ReadAim(); });
	Command("aim_member", [this](Args const& a) { if (!a.empty()) AimShow(std::atoi(a[0].c_str())); });
	Command("aim_step", [this](Args const& a) { if (!a.empty()) AimStep(std::atoi(a[0].c_str())); });
	Command("aim_call", [this](Args const&) { AimCall(); });
	Command("aim_hangup", [this](Args const&) { AimHangUp(); });
	Command("aim_length", [this](Args const& a) { if (!a.empty()) { hireLength = std::clamp(std::atoi(a[0].c_str()), 0, 2); AimCosts(); } });
	Command("aim_gear", [this](Args const& a) { if (!a.empty()) { hireGear = a[0] == "1"; AimCosts(); } });
	Command("aim_hire", [this](Args const&) { AimToHire(); });
	Command("aim_transfer", [this](Args const&) { AimTransfer(); });
	Command("aim_message", [this](Args const&) { AimMessage(); });

	// M.E.R.C.
	Command("merc_open_account", [this](Args const&) { LaptopNative::MercOpenAccount(); speck = S(LaptopNative::SpeckSays()); ReadMerc(); });
	Command("merc_hire", [this](Args const& a) { if (!a.empty()) MercHire(std::atoi(a[0].c_str())); });
	Command("merc_pay", [this](Args const&) {
		if (LaptopNative::MercOwed() == 0) return;
		if (onAsk) onAsk("merc_pay", "");
	});

	// Bobby Ray's
	Command("bobby_cat", [this](Args const& a) { if (!a.empty()) { bobbyCat = std::clamp(std::atoi(a[0].c_str()), 0, 4); GoPage("shop"); } });
	Command("bobby_add", [this](Args const& a) { if (!a.empty()) { LaptopNative::ShopAdd(LaptopNative::Shop(bobbyCat), UINT16(std::atoi(a[0].c_str()))); ReadBobby(); } });
	Command("bobby_remove", [this](Args const& a) { if (!a.empty()) { LaptopNative::ShopRemove(LaptopNative::Shop(bobbyCat), UINT16(std::atoi(a[0].c_str()))); ReadBobby(); } });
	Command("order_line", [this](Args const& a) { if (a.size() >= 2) { LaptopNative::OrderChange(std::atoi(a[0].c_str()), std::atoi(a[1].c_str())); ReadBobby(); } });
	Command("order_remove", [this](Args const& a) { if (!a.empty()) { LaptopNative::OrderRemoveLine(std::atoi(a[0].c_str())); ReadBobby(); } });
	Command("order_clear", [this](Args const&) { LaptopNative::OrderClear(); ReadBobby(); });
	Command("order_city", [this](Args const& a) { if (!a.empty()) { orderCity = std::atoi(a[0].c_str()); cityOpen = false; ReadBobby(); } });
	Command("order_city_menu", [this](Args const&) { cityOpen = !cityOpen; Changed(); });
	Command("order_speed", [this](Args const& a) { if (!a.empty()) { orderSpeed = std::clamp(std::atoi(a[0].c_str()), 0, 2); ReadBobby(); } });
	Command("order_accept", [this](Args const&) { OrderAccept(); });

	RegisterSiteCommands();
}

void LaptopViewModel::Load()
{
	labels.clear();
	for (int i = 0; i < 7; ++i) appLabels[i] = S(pLaptopIcons[i]);
	labels["mail"] = appLabels[0]; labels["web"] = appLabels[1]; labels["finances"] = appLabels[2];
	labels["personnel"] = appLabels[3]; labels["history"] = appLabels[4]; labels["files"] = appLabels[5];
	labels["close"] = appLabels[6];
	labels["from"] = S(pEmailHeaders[0]); labels["subject"] = S(pEmailHeaders[1]); labels["day"] = S(pEmailHeaders[2]);
	labels["mailbox"] = S(pEmailTitleText); labels["fileviewer"] = S(pFilesTitle); labels["historylog"] = S(pHistoryTitle);
	labels["bookkeeper"] = S(pFinanceTitle);
	labels["h_day"] = S(pHistoryHeaders[0]); labels["h_location"] = S(pHistoryHeaders[3]); labels["h_event"] = S(pHistoryHeaders[4]);
	labels["f_day"] = S(pFinanceHeaders[0]); labels["f_credit"] = S(pFinanceHeaders[1]); labels["f_debit"] = S(pFinanceHeaders[2]);
	labels["f_tx"] = S(pFinanceHeaders[3]); labels["f_balance"] = S(pFinanceHeaders[4]);
	for (int i = 2; i < 12; ++i) labels["fs" + std::to_string(i)] = S(pFinanceSummary[i]);
	labels["team_current"] = S(pPersonelTeamStrings[0]); labels["team_departed"] = S(pPersonelTeamStrings[1]);
	labels["team_daily"] = S(pPersonelTeamStrings[2]); labels["team_high"] = S(pPersonelTeamStrings[3]); labels["team_low"] = S(pPersonelTeamStrings[4]);
	for (auto const* k : { "desktop", "notifications", "bookmarks", "choose", "stats", "employment", "inventory", "record", "attributes",
		"no_mail", "no_files", "close_message", "delete", "previous", "next", "sort_by", "show_all", "show_available", "contact",
		"call", "hangup", "leave_message", "hire", "transfer", "balance_after", "contract_charge", "equipment", "medical", "on_team",
		"away", "deceased", "unavailable", "gear", "contract_length", "account", "files_tab", "owed", "pay", "open_account",
		"cart", "order_form", "shop", "shipments", "clear_order", "accept_order", "delivery", "speed", "subtotal", "shipping",
		"grand_total", "weight", "in_stock", "each", "back_to_shop", "no_items", "map", "help", "loading", "rain", "nothing" })
		labels[k] = Str(std::string("laptop.") + k);
	labels["fee"] = S(CharacterInfo[AIM_MEMBER_FEE]);
	labels["one_day"] = S(VideoConfercingText[AIM_MEMBER_ONE_DAY]); labels["one_week"] = S(VideoConfercingText[AIM_MEMBER_ONE_WEEK]);
	labels["two_weeks"] = S(VideoConfercingText[AIM_MEMBER_TWO_WEEKS]);
	labels["no_equipment"] = S(VideoConfercingText[AIM_MEMBER_NO_EQUIPMENT]); labels["buy_equipment"] = S(VideoConfercingText[AIM_MEMBER_BUY_EQUIPMENT]);
	labels["additional"] = S(CharacterInfo[AIM_MEMBER_ADDTNL_INFO]);
	for (int i = 0; i < 6; ++i) aimSortLabels[i] = S(str_aim_sort_list[i]);
	for (int i = 0; i < 11; ++i) statLabels[i] = S(str_stat_list[i]);
	LoadSiteLabels();
	app = "desktop";
	site.clear();
	page.clear();
	history.clear();
	historyPos = -1;
	Refresh();
}

void LaptopViewModel::Refresh()
{
	day = std::to_string(GetWorldDay());
	time = ST::format("{02d}:{02d}", GetWorldHour(), GetWorldMinutesInDay() % 60).to_std_string();
	balance = Money(LaptopSaveInfo.iCurrentBalance);
	int n = 0;
	CFOR_EACH_IN_TEAM(s, OUR_TEAM) if (s->bActive && !(s->uiStatusFlags & SOLDIER_VEHICLE)) ++n;
	team = std::to_string(n);
	unreadMail = LaptopNative::UnreadMails();
	unreadFiles = LaptopNative::UnreadFiles();
	ReadDesktop();
	ReadBookmarks();
	if (app == "email") ReadMail();
	else if (app == "files") ReadFiles();
	else if (app == "history") ReadHistory();
	else if (app == "finances") ReadFinances();
	else if (app == "personnel") ReadPersonnel();
	else if (app == "web") ReadSite();
	Changed();
}

// ---- navigation ------------------------------------------------------------------------------------------------

void LaptopViewModel::OpenApp(std::string const& a)
{
	if (a == "web")
	{
		app = "web";
		if (site.empty())
		{
			auto const bm = LaptopNative::Bookmarks();
			OpenSite(bm.empty() ? "aim" : g_sites[0].id);
			return;
		}
		Navigate(site, page, true);
		return;
	}
	app = a;
	static std::map<std::string, int> const modes = { { "desktop", LAPTOP_MODE_NONE }, { "email", LAPTOP_MODE_EMAIL },
		{ "files", LAPTOP_MODE_FILES }, { "history", LAPTOP_MODE_HISTORY }, { "finances", LAPTOP_MODE_FINANCES }, { "personnel", LAPTOP_MODE_PERSONNEL } };
	auto const m = modes.find(a);
	LaptopNative::SetMode(m == modes.end() ? LAPTOP_MODE_NONE : m->second);
	mailOpen = -1;
	fileOpen = -1;
	persSelected = 0;
	Refresh();
}

void LaptopViewModel::OpenSite(std::string const& id)
{
	SiteDef const* s = FindSite(id);
	if (!s) return;
	bool listed = false;
	for (int b : LaptopNative::Bookmarks()) listed |= b == s->bookmark;
	if (!listed) return;
	app = "web";
	firstVisit = LaptopNative::VisitSite(s->bookmark);
	loading = firstVisit ? 100 : 0;
	if (onAction && firstVisit && LaptopNative::IsRaining()) onAction("rain");
	Navigate(id, "", true);
}

void LaptopViewModel::GoPage(std::string const& p) { Navigate(site, p, true); }

void LaptopViewModel::History(int const delta)
{
	int const to = historyPos + delta;
	if (to < 0 || to >= int(history.size())) return;
	historyPos = to;
	app = "web";
	Navigate(history[to].first, history[to].second, false);
}

void LaptopViewModel::Navigate(std::string const& s, std::string p, bool const record)
{
	SiteDef const* def = FindSite(s);
	if (!def) return;
	if (p.empty())
	{
		// each site's home page
		if (s == "aim") p = "members";
		else if (s == "merc") p = LaptopNative::MercSiteDown() ? "broken" : "home";
		else if (s == "bobby") p = LaptopNative::BobbyRayOpen() ? "shop" : "closed";
		else if (s == "imp") p = "home";
		else if (s == "florist") p = "home";
		else if (s == "insurance") p = "home";
		else p = "home";
	}
	bool const enteringMerc = s == "merc" && (site != "merc" || app != "web") && p == "home";
	site = s;
	page = p;
	if (record)
	{
		if (historyPos + 1 < int(history.size())) history.resize(historyPos + 1);
		if (history.empty() || history.back() != std::make_pair(s, p)) history.emplace_back(s, p);
		historyPos = int(history.size()) - 1;
	}
	canBack = historyPos > 0;
	canForward = historyPos + 1 < int(history.size());
	host = def->host;
	path = "/" + p;
	LaptopNative::SetMode(SiteMode());
	if (enteringMerc) speck = S(LaptopNative::SpeckSays());
	if (s != "aim" || p != "member") hireState.clear();
	Refresh();
}

int LaptopViewModel::SiteMode() const
{
	if (site == "aim")
	{
		if (page == "member") return LAPTOP_MODE_AIM_MEMBERS;
		if (page == "members") return LAPTOP_MODE_AIM_MEMBERS_FACIAL_INDEX;
		if (page == "alumni") return LAPTOP_MODE_AIM_MEMBERS_ARCHIVES;
		if (page == "policies") return LAPTOP_MODE_AIM_POLICIES;
		if (page == "history") return LAPTOP_MODE_AIM_HISTORY;
		if (page == "links") return LAPTOP_MODE_AIM_LINKS;
		return LAPTOP_MODE_AIM;
	}
	if (site == "merc")
	{
		if (page == "broken") return LAPTOP_MODE_BROKEN_LINK;
		if (page == "account") return LaptopNative::MercAccountStatus() == 0 ? LAPTOP_MODE_MERC_NO_ACCOUNT : LAPTOP_MODE_MERC_ACCOUNT;
		return LAPTOP_MODE_MERC;
	}
	if (site == "bobby")
	{
		if (page == "order") return LAPTOP_MODE_BOBBY_R_MAILORDER;
		if (page == "shipments") return LAPTOP_MODE_BOBBYR_SHIPMENTS;
		if (page == "shop")
		{
			static int const modes[] = { LAPTOP_MODE_BOBBY_R_GUNS, LAPTOP_MODE_BOBBY_R_AMMO, LAPTOP_MODE_BOBBY_R_ARMOR, LAPTOP_MODE_BOBBY_R_MISC, LAPTOP_MODE_BOBBY_R_USED };
			return modes[bobbyCat];
		}
		return LAPTOP_MODE_BOBBY_R;
	}
	if (site == "imp") return LAPTOP_MODE_CHAR_PROFILE;
	if (site == "florist")
	{
		if (page == "gallery") return LAPTOP_MODE_FLORIST_FLOWER_GALLERY;
		if (page == "order") return LAPTOP_MODE_FLORIST_ORDERFORM;
		if (page == "cards") return LAPTOP_MODE_FLORIST_CARD_GALLERY;
		return LAPTOP_MODE_FLORIST;
	}
	if (site == "insurance")
	{
		if (page == "info") return LAPTOP_MODE_INSURANCE_INFO;
		if (page == "contract") return LAPTOP_MODE_INSURANCE_CONTRACT;
		if (page == "comments") return LAPTOP_MODE_INSURANCE_COMMENTS;
		return LAPTOP_MODE_INSURANCE;
	}
	return LAPTOP_MODE_FUNERAL;
}

void LaptopViewModel::ReadBookmarks()
{
	bookmarks.clear();
	for (int const b : LaptopNative::Bookmarks())
	{
		for (SiteDef const& s : g_sites)
		{
			if (s.bookmark != b) continue;
			Row r;
			r.id = s.id; r.title = S(pBookMarkStrings[b]); r.cls = s.cls; r.sub = s.host;
			r.on = app == "web" && site == s.id;
			r.alt = !LaptopSaveInfo.fVisitedBookmarkAlready[b];
			bookmarks.push_back(r);
		}
	}
	webTitle.clear();
	if (app == "web")
	{
		int const m = SiteMode();
		int const idx = m - LAPTOP_MODE_AIM;
		if (idx >= 0 && idx < int(pWebPagesTitles.size())) webTitle = S(pWebPagesTitles[idx]);
	}
}

void LaptopViewModel::ReadDesktop()
{
	notes.clear();
	int k = 0;
	for (Email* m : LaptopNative::Mails())
	{
		if (m->fRead) continue;
		Row r; r.index = k++; r.icon = "mail"; r.title = S(LaptopNative::MailSender(*m)); r.sub = S(m->pSubject); r.meta = labels["day"] + " " + Day(m->iDate);
		notes.push_back(r);
	}
	for (auto const& f : LaptopNative::Files())
	{
		if (f.read) continue;
		Row r; r.index = k++; r.icon = "folder"; r.title = labels["files"]; r.sub = S(f.title);
		notes.push_back(r);
	}
	CFOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (!s->bActive || s->ubWhatKindOfMercAmI != MERC_TYPE__AIM_MERC) continue;
		INT32 const left = s->iEndofContractTime - INT32(GetWorldTotalMin());
		if (left < 0 || left > 24 * 60) continue;
		Row r; r.index = k++; r.icon = "contract"; r.title = S(s->name); r.sub = S(pPersonnelScreenStrings[PRSNL_TXT_CURRENT_CONTRACT]);
		r.meta = std::to_string(left / 60) + S(gpStrategicString[STR_PB_HOURS_ABBREVIATION]);
		notes.push_back(r);
	}
	noteCount = int(notes.size());
}

// ---- e-mail ------------------------------------------------------------------------------------------------------

void LaptopViewModel::ReadMail()
{
	mails.clear();
	mailPtrs = LaptopNative::Mails();
	std::vector<int> order(mailPtrs.size());
	for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
	auto key = [&](int a, int b) {
		Email const& x = *mailPtrs[a]; Email const& y = *mailPtrs[b];
		if (mailSort == "from") return LaptopNative::MailSender(x).compare(LaptopNative::MailSender(y)) < 0;
		if (mailSort == "subject") return x.pSubject.compare(y.pSubject) < 0;
		return x.iDate < y.iDate;
	};
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return mailSortDesc ? key(b, a) : key(a, b); });
	for (int const i : order)
	{
		Email const& m = *mailPtrs[i];
		Row r; r.index = i; r.title = S(LaptopNative::MailSender(m)); r.sub = S(m.pSubject).substr(m.pSubject.size() && m.pSubject.c_str()[0] == ' ' ? 1 : 0);
		r.meta = Day(m.iDate); r.alt = !m.fRead; r.on = i == mailOpen;
		mails.push_back(r);
	}
	mailCount = int(mails.size());
	unreadMail = LaptopNative::UnreadMails();
	if (mailOpen < 0) { mailBody.clear(); mailSubject.clear(); }
	Changed();
}

void LaptopViewModel::MailOpen(int const i)
{
	mailPtrs = LaptopNative::Mails();
	if (i < 0 || i >= int(mailPtrs.size())) return;
	Email& m = *mailPtrs[i];
	mailOpen = i;
	mailBody.clear();
	for (ST::string const& p : LaptopNative::OpenMail(m)) { std::string t = Clean(p); if (!t.empty()) mailBody.push_back(t); }
	mailSubject = S(m.pSubject);
	if (!mailSubject.empty() && mailSubject[0] == ' ') mailSubject.erase(0, 1);
	mailFrom = S(LaptopNative::MailSender(m));
	mailDay = labels["day"] + " " + Day(m.iDate);
	Refresh();
}

void LaptopViewModel::MailStep(int const delta)
{
	if (mails.empty()) return;
	int pos = -1;
	for (size_t k = 0; k < mails.size(); ++k) if (mails[k].index == mailOpen) pos = int(k);
	pos = std::clamp(pos + delta, 0, int(mails.size()) - 1);
	MailOpen(mails[pos].index);
}

void LaptopViewModel::MailDelete(int const i)
{
	mailPtrs = LaptopNative::Mails();
	if (i < 0 || i >= int(mailPtrs.size())) return;
	LaptopNative::DeleteMail(mailPtrs[i]);
	mailOpen = -1;
	Refresh();
}

// ---- files, history, finances --------------------------------------------------------------------------------------

void LaptopViewModel::ReadFiles()
{
	files.clear();
	for (auto const& f : LaptopNative::Files())
	{
		Row r; r.index = f.index; r.title = S(f.title); r.alt = !f.read; r.on = f.index == fileOpen;
		files.push_back(r);
	}
	fileCount = int(files.size());
	if (fileOpen < 0) fileBody.clear();
	Changed();
}

void LaptopViewModel::FileOpen(int const i)
{
	fileOpen = i;
	fileBody.clear();
	for (ST::string const& p : LaptopNative::OpenFile(i)) { std::string t = Clean(p); if (!t.empty()) fileBody.push_back(t); }
	unreadFiles = LaptopNative::UnreadFiles();
	ReadFiles();
}

void LaptopViewModel::ReadHistory()
{
	historyRows.clear();
	auto const rows = LaptopNative::History();
	UINT32 lastDay = UINT32(-1);
	int i = 0;
	for (auto it = rows.rbegin(); it != rows.rend(); ++it, ++i)
	{
		UINT32 const d = it->date / (24 * 60);
		Row r; r.index = i; r.meta = std::to_string(d); r.sub = S(it->location); r.title = S(it->text); r.alt = it->open;
		r.cls = d != lastDay ? "first" : "";
		lastDay = d;
		historyRows.push_back(r);
	}
	historyCount = int(historyRows.size());
	Changed();
}

void LaptopViewModel::ReadFinances()
{
	auto const s = LaptopNative::Summary();
	finBalance = Money(s.balance);
	finTodayIncome = Money(s.todayIncome);
	finTodayOther = Money(s.todayOther);
	finTodayDebits = Money(-std::abs(s.todayDebits));
	finForecast = Money(s.forecastIncome);
	finProjected = Money(s.projectedBalance);
	finYIncome = Money(s.yesterdayIncome);
	finYOther = Money(s.yesterdayOther);
	finYDebits = Money(-std::abs(s.yesterdayDebits));
	finYBalance = Money(s.yesterdayBalance);
	ledger.clear();
	auto const tx = LaptopNative::Transactions();
	int i = 0;
	UINT32 lastDay = UINT32(-1);
	for (auto it = tx.rbegin(); it != tx.rend(); ++it, ++i)
	{
		UINT32 const d = it->date / (24 * 60);
		Row r; r.index = i; r.meta = std::to_string(d); r.title = S(it->text);
		r.sub = it->amount >= 0 ? Money(it->amount) : ""; r.value = it->amount < 0 ? Money(-it->amount) : "";
		r.extra = Money(it->balance); r.cls = d != lastDay ? "first" : "";
		lastDay = d;
		ledger.push_back(r);
	}
	ledgerCount = int(ledger.size());
	// the balance at the end of each of the last 12 days, for the chart
	chart.clear();
	std::map<UINT32, INT32> endOfDay;
	for (auto const& t : tx) endOfDay[t.date / (24 * 60)] = t.balance;
	INT32 maxV = 1;
	for (auto const& [d, b] : endOfDay) maxV = std::max(maxV, std::abs(b));
	int k = 0;
	for (auto it = endOfDay.size() > 12 ? std::next(endOfDay.begin(), endOfDay.size() - 12) : endOfDay.begin(); it != endOfDay.end(); ++it, ++k)
	{
		Row r; r.index = k; r.meta = std::to_string(it->first); r.title = Money(it->second); r.alt = it->second < 0;
		r.n = std::max(4, std::abs(it->second) * 100 / maxV);
		chart.push_back(r);
	}
	Changed();
}

// ---- personnel ---------------------------------------------------------------------------------------------------

void LaptopViewModel::ReadPersonnel()
{
	roster.clear();
	persSoldiers.clear();
	INT32 daily = 0;
	std::string highName, lowName; int high = -1, low = 1 << 30;
	if (persTab == "current")
	{
		CFOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (!s->bActive || (s->uiStatusFlags & SOLDIER_VEHICLE)) continue;
			MERCPROFILESTRUCT const& p = GetProfile(s->ubProfile);
			Row r; r.index = int(roster.size()); r.id = std::to_string(s->ubProfile);
			r.title = S(s->name);
			r.sub = S(pAssignmentStrings[s->bAssignment]) + (s->sSector.IsValid() ? " \xC2\xB7 " + S(s->sSector.AsShortString()) : "");
			r.n = s->bLifeMax ? s->bLife * 100 / s->bLifeMax : 0;
			r.icon = "face-" + std::to_string(p.ubFaceIndex);
			if (s->ubWhatKindOfMercAmI == MERC_TYPE__AIM_MERC)
			{
				INT32 const left = std::max(0, s->iEndofContractTime - INT32(GetWorldTotalMin()));
				r.meta = left >= 24 * 60 ? ST::format("{.1f}d", left / 1440.0f).to_std_string() : std::to_string(left / 60) + "h";
				r.alt = left < 24 * 60;
			}
			else r.meta = "\xE2\x80\x94";
			r.on = r.index == persSelected;
			roster.push_back(r);
			persSoldiers.push_back(s->ubProfile);
			daily += p.sSalary;
			if (p.sSalary > high) { high = p.sSalary; highName = S(s->name); }
			if (p.sSalary < low) { low = p.sSalary; lowName = S(s->name); }
		}
	}
	else
	{
		auto add = [&](INT16 const* list, int state) {
			for (int i = 0; i < 256; ++i)
			{
				if (list[i] == -1) continue;
				MERCPROFILESTRUCT const& p = GetProfile(ProfileID(list[i]));
				Row r; r.index = int(roster.size()); r.id = std::to_string(list[i]); r.title = S(p.zNickname);
				r.sub = S(pPersonnelDepartedStateStrings[state]); r.icon = "face-" + std::to_string(p.ubFaceIndex);
				r.cls = state == 0 ? "dead" : ""; r.meta = ""; r.on = r.index == persSelected;
				roster.push_back(r);
				persSoldiers.push_back(ProfileID(list[i]));
			}
		};
		add(LaptopSaveInfo.ubDeadCharactersList, 0);
		add(LaptopSaveInfo.ubLeftCharactersList, 1);
		add(LaptopSaveInfo.ubOtherCharactersList, 4);
	}
	rosterCount = int(roster.size());
	teamDaily = Money(daily);
	teamHigh = high >= 0 ? highName + " " + Money(high) : "\xE2\x80\x94";
	teamLow = high >= 0 ? lowName + " " + Money(low) : "\xE2\x80\x94";
	persSelected = std::clamp(persSelected, 0, std::max(0, rosterCount - 1));
	for (Row& r : roster) r.on = r.index == persSelected;

	pdHas = rosterCount > 0;
	pdAttrs.clear(); pdEmployment.clear(); pdRecord.clear(); pdItems.clear();
	if (!pdHas) { Changed(); return; }
	ProfileID const pid = persSoldiers[persSelected];
	MERCPROFILESTRUCT const& p = GetProfile(pid);
	SOLDIERTYPE const* const s = FindSoldierByProfileIDOnPlayerTeam(pid);
	pdName = S(p.zNickname); pdFull = S(p.zName);
	pdFace = "face-" + std::to_string(p.ubFaceIndex);
	pdDead = persTab != "current" && roster[persSelected].cls == "dead";
	if (s)
	{
		pdHp = ST::format("{}/{}", s->bLife, s->bLifeMax).to_std_string(); pdHpPct = Pct(s->bLife, s->bLifeMax);
		pdBreath = std::to_string(s->bBreath); pdBreathPct = Pct(s->bBreath);
		pdMorale = S(GetMoraleString(*s)); pdMoralePct = Pct(s->bMorale);
		pdWhere = S(pAssignmentStrings[s->bAssignment]) + (s->sSector.IsValid() ? " \xC2\xB7 " + S(GetSectorIDString(s->sSector, FALSE)) : "");
	}
	else { pdHp = pdBreath = pdMorale = pdWhere = ""; pdHpPct = pdBreathPct = pdMoralePct = "0%"; }
	INT8 const stats[] = { s ? s->bLifeMax : p.bLifeMax, s ? s->bAgility : p.bAgility, s ? s->bDexterity : p.bDexterity,
		s ? s->bStrength : p.bStrength, s ? s->bLeadership : p.bLeadership, s ? s->bWisdom : p.bWisdom, s ? s->bExpLevel : p.bExpLevel,
		s ? s->bMarksmanship : p.bMarksmanship, s ? s->bMechanical : p.bMechanical, s ? s->bExplosive : p.bExplosive, s ? s->bMedical : p.bMedical };
	for (int i = 0; i < 11; ++i) { Row r; r.index = i; r.title = statLabels[i]; r.value = std::to_string(stats[i]); pdAttrs.push_back(r); }
	auto kv = [](std::vector<Row>& v, std::string k, std::string val) { Row r; r.index = int(v.size()); r.title = std::move(k); r.value = std::move(val); v.push_back(r); };
	ST::string const dAbbr = gpStrategicString[STR_PB_DAYS_ABBREVIATION];
	if (s)
	{
		std::string contract = S(gpStrategicString[STR_PB_NOTAPPLICABLE_ABBREVIATION]);
		if (s->ubWhatKindOfMercAmI == MERC_TYPE__AIM_MERC || s->ubProfile == SLAY)
		{
			INT32 left = std::max(0, s->iEndofContractTime - INT32(GetWorldTotalMin()));
			contract = ST::format("{}{} {}{} / {}{}", left / 1440, dAbbr, left % 1440 / 60, gpStrategicString[STR_PB_HOURS_ABBREVIATION], s->iTotalContractLength, dAbbr).to_std_string();
		}
		kv(pdEmployment, S(pPersonnelScreenStrings[PRSNL_TXT_CURRENT_CONTRACT]), contract);
		INT32 salary = p.sSalary;
		if (s->ubWhatKindOfMercAmI == MERC_TYPE__AIM_MERC)
		{
			if (s->bTypeOfLastContract == CONTRACT_EXTEND_2_WEEK) salary = p.uiBiWeeklySalary / 14;
			else if (s->bTypeOfLastContract == CONTRACT_EXTEND_1_WEEK) salary = p.uiWeeklySalary / 7;
		}
		kv(pdEmployment, S(pPersonnelScreenStrings[PRSNL_TXT_DAILY_COST]), Money(salary));
		if (s->ubWhatKindOfMercAmI == MERC_TYPE__MERC)
			kv(pdEmployment, S(pPersonnelScreenStrings[PRSNL_TXT_UNPAID_AMOUNT]), Money(p.sSalary * p.iMercMercContractLength));
		else
			kv(pdEmployment, S(pPersonnelScreenStrings[PRSNL_TXT_MED_DEPOSIT]), Money(p.sMedicalDepositAmount));
		for (int i = 0; i < NUM_INV_SLOTS; ++i)
		{
			OBJECTTYPE const& o = s->inv[i];
			if (o.usItem == NOTHING) continue;
			Row r; r.index = int(pdItems.size()); r.id = std::to_string(o.usItem); r.title = S(GCM->getItem(o.usItem)->getName());
			r.value = o.ubNumberOfObjects > 1 ? "\xC3\x97" + std::to_string(o.ubNumberOfObjects) : std::to_string(o.bStatus[0]) + "%";
			pdItems.push_back(r);
		}
	}
	kv(pdEmployment, S(pPersonnelScreenStrings[PRSNL_TXT_TOTAL_COST]), Money(p.uiTotalCostToDate));
	kv(pdEmployment, S(pPersonnelScreenStrings[PRSNL_TXT_TOTAL_SERVICE]), ST::format("{} {}", p.usTotalDaysServed, dAbbr).to_std_string());
	kv(pdRecord, S(pPersonnelScreenStrings[PRSNL_TXT_KILLS]), std::to_string(p.usKills));
	kv(pdRecord, S(pPersonnelScreenStrings[PRSNL_TXT_ASSISTS]), std::to_string(p.usAssists));
	kv(pdRecord, S(pPersonnelScreenStrings[PRSNL_TXT_HIT_PERCENTAGE]), p.usShotsFired ? std::to_string(p.usShotsHit * 100 / p.usShotsFired) + "%" : "0%");
	kv(pdRecord, S(pPersonnelScreenStrings[PRSNL_TXT_BATTLES]), std::to_string(p.usBattlesFought));
	kv(pdRecord, S(pPersonnelScreenStrings[PRSNL_TXT_TIMES_WOUNDED]), std::to_string(p.usTimesWounded));
	itemCount = int(pdItems.size());
	Changed();
}

void LaptopViewModel::Describe(Fields& f)
{
	f.Field("app", app); f.Field("site", site); f.Field("page", page);
	f.Field("day", day); f.Field("time", time); f.Field("balance", balance); f.Field("team", team);
	f.Field("unread_mail", unreadMail); f.Field("unread_files", unreadFiles);
	f.Rows("notes", notes); f.Field("note_count", noteCount);
	f.Rows("bookmarks", bookmarks); f.Field("host", host); f.Field("path", path); f.Field("web_title", webTitle);
	f.Field("can_back", canBack); f.Field("can_forward", canForward); f.Field("loading", loading);
	f.Rows("mails", mails); f.Field("mail_count", mailCount); f.Field("mail_open", mailOpen); f.Field("mail_sort", mailSort); f.Field("mail_sort_desc", mailSortDesc);
	f.List("mail_body", mailBody); f.Field("mail_subject", mailSubject); f.Field("mail_from", mailFrom); f.Field("mail_day", mailDay);
	f.Rows("files", files); f.Field("file_count", fileCount); f.Field("file_open", fileOpen); f.List("file_body", fileBody);
	f.Rows("history_rows", historyRows); f.Field("history_count", historyCount);
	f.Field("fin_balance", finBalance); f.Field("fin_today_income", finTodayIncome); f.Field("fin_today_other", finTodayOther);
	f.Field("fin_today_debits", finTodayDebits); f.Field("fin_forecast", finForecast); f.Field("fin_projected", finProjected);
	f.Field("fin_y_income", finYIncome); f.Field("fin_y_other", finYOther); f.Field("fin_y_debits", finYDebits); f.Field("fin_y_balance", finYBalance);
	f.Rows("ledger", ledger); f.Field("ledger_count", ledgerCount); f.Rows("chart", chart);
	f.Rows("roster", roster); f.Field("roster_count", rosterCount); f.Field("pers_tab", persTab); f.Field("pers_view", persView);
	f.Field("pers_selected", persSelected);
	f.Field("team_daily", teamDaily); f.Field("team_high", teamHigh); f.Field("team_low", teamLow);
	f.Field("pd_has", pdHas); f.Field("pd_name", pdName); f.Field("pd_full", pdFull); f.Field("pd_face", pdFace); f.Field("pd_where", pdWhere);
	f.Field("pd_dead", pdDead);
	f.Field("pd_hp", pdHp); f.Field("pd_hp_pct", pdHpPct); f.Field("pd_breath", pdBreath); f.Field("pd_breath_pct", pdBreathPct);
	f.Field("pd_morale", pdMorale); f.Field("pd_morale_pct", pdMoralePct);
	f.Rows("pd_attrs", pdAttrs); f.Rows("pd_employment", pdEmployment); f.Rows("pd_record", pdRecord); f.Rows("pd_items", pdItems);
	f.Field("item_count", itemCount);
	for (auto& [k, v] : labels) f.Field(("l_" + k).c_str(), v);
	DescribeSites(f);
}

}
