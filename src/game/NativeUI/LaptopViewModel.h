#pragma once
// The native laptop's view model (docs/ui/laptop.md): one model for the shell and every program and site, so that
// the RML document and automation see one state. LaptopScreen.cc has the shell and the programs, LaptopSites.cc the
// web sites. ja2.viewModel("laptop") reads it; commands are what the buttons call.

#include "ViewModel.h"

#include "JA2Types.h"

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct Email;

namespace NativeUI
{
	/** A generic row: what each list needs, named for the RML (r.title, r.sub, ...). */
	struct LaptopRow
	{
		int index = 0, n = 0, m = 0;
		std::string id, title, sub, meta, value, extra, icon, cls;
		bool on = false, alt = false;
		static void Describe(RowFields<LaptopRow>& f)
		{
			f("index", &LaptopRow::index)("n", &LaptopRow::n)("m", &LaptopRow::m)("id", &LaptopRow::id)("title", &LaptopRow::title)
			 ("sub", &LaptopRow::sub)("meta", &LaptopRow::meta)("value", &LaptopRow::value)("extra", &LaptopRow::extra)
			 ("icon", &LaptopRow::icon)("cls", &LaptopRow::cls)("on", &LaptopRow::on)("alt", &LaptopRow::alt);
		}
	};

	class LaptopViewModel final : public ViewModel
	{
	public:
		using Row = LaptopRow;
		LaptopViewModel();
		/** Labels and the start state (the desktop). */
		void Load();
		void Refresh() override;
		void Describe(Fields&) override;

		/** The screen: "close" (to the map), "help", "rain" (the slow connection notice). */
		std::function<void(std::string const&)> onAction;
		/** The screen asks the player (a message box) before @a what ("mail_delete", "merc_pay", "order") with @a arg;
		 * Confirm() runs it after Yes. */
		std::function<void(std::string const&, std::string const&)> onAsk;
		void Confirm(std::string const& what, std::string const& arg);
		/** A short message for the player (a toast). */
		std::function<void(std::string const&, int kind)> onToast;

		void OpenApp(std::string const&);
		void OpenSite(std::string const&);
		void GoPage(std::string const&);
		void MailStep(int delta);
		void MailDelete(int index);

		// state, public for the screen and the tests
		std::string app, site, page;
		int mailOpen = -1, fileOpen = -1;
		std::string hireState; // "", "calling", "talk", "hire", "machine", "unavailable", "refused", "hired"

	private:
		void History(int delta);
		void Navigate(std::string const& site, std::string page, bool record);
		int  SiteMode() const;
		void ReadBookmarks();
		void ReadDesktop();
		void ReadMail();
		void MailOpen(int);
		void ReadFiles();
		void FileOpen(int);
		void ReadHistory();
		void ReadFinances();
		void ReadPersonnel();

		// sites (LaptopSites.cc)
		void RegisterSiteCommands();
		void LoadSiteLabels();
		void DescribeSites(Fields&);
		void ReadSite();
		void ReadAim();
		void AimShow(int profile);
		void AimStep(int delta);
		void AimCall();
		void AimHangUp();
		void AimCosts();
		void AimToHire();
		void AimTransfer();
		void AimMessage();
		void ReadMerc();
		void MercHire(int profile);
		void ReadBobby();
		void OrderAccept();
		void ReadImp();
		void ReadFlorist();
		void ReadInsurance();

		std::map<std::string, std::string> labels;
		std::string appLabels[7], aimSortLabels[6], statLabels[11];

		std::string day, time, balance, team;
		int unreadMail = 0, unreadFiles = 0;
		std::vector<Row> notes; int noteCount = 0;
		std::vector<Row> bookmarks;
		std::string host, path, webTitle;
		bool canBack = false, canForward = false, firstVisit = false;
		int loading = 0;
		std::vector<std::pair<std::string, std::string>> history;
		int historyPos = -1;

		std::vector<Email*> mailPtrs;
		std::vector<Row> mails; int mailCount = 0;
		std::string mailSort = "day"; bool mailSortDesc = true;
		std::vector<std::string> mailBody; std::string mailSubject, mailFrom, mailDay;

		std::vector<Row> files; int fileCount = 0; std::vector<std::string> fileBody;
		std::vector<Row> historyRows; int historyCount = 0;

		std::string finBalance, finTodayIncome, finTodayOther, finTodayDebits, finForecast, finProjected;
		std::string finYIncome, finYOther, finYDebits, finYBalance;
		std::vector<Row> ledger, chart; int ledgerCount = 0;

		std::vector<Row> roster; int rosterCount = 0; std::vector<ProfileID> persSoldiers;
		std::string persTab = "current", persView = "stats"; int persSelected = 0;
		std::string teamDaily, teamHigh, teamLow;
		bool pdHas = false, pdDead = false;
		std::string pdName, pdFull, pdFace, pdWhere, pdHp, pdHpPct = "0%", pdBreath, pdBreathPct = "0%", pdMorale, pdMoralePct = "0%";
		std::vector<Row> pdAttrs, pdEmployment, pdRecord, pdItems; int itemCount = 0;

		// A.I.M.
		std::vector<Row> aimMembers; std::string aimSort = "price"; bool aimSortDesc = false, aimAvailableOnly = false;
		std::vector<Row> aimSortChips;
		int aimProfile = -1;
		std::string amName, amFull, amFace, amFee1, amFee7, amFee14, amMedical, amGearCost, amStatus;
		std::vector<Row> amStats, amGear; std::vector<std::string> amBio;
		int hireLength = 1; bool hireGear = false;
		std::string hireQuote, hireContract, hireEquipment, hireDeposit, hireTotal, hireAfter, hireMessage;
		bool hireAffordable = true;
		std::vector<std::string> aimText;

		// M.E.R.C.
		std::vector<Row> mercFiles; std::string speck, mercOwed, mercAccount; int mercStatus = 0; bool mercHasAccount = false;
		std::vector<Row> mercAccountRows;

		// Bobby Ray's
		int bobbyCat = 0; std::vector<Row> shop, cart, orderLines, cities, shipments, bobbyCats; int shopCount = 0, cartCount = 0;
		std::string cartTotal, cartWeight, cartCountText;
		int orderCity = -1, orderSpeed = 1; bool cityOpen = false, orderReady = false;
		std::string cityName, orderSubtotal, orderWeight, orderShipping, orderTotal;
		std::vector<Row> speeds;

		// I.M.P., florist, insurance, mortuary (LaptopSites.cc)
		std::string impStep = "home", impCode, impError, impFull, impNick, impCost; bool impFemale = false, impCanCreate = true;
		int impQuestion = 0, impPortrait = 0, impVoice = 0, impBonus = 0;
		std::vector<int> impAnswers; std::vector<Row> impAnswerRows, impSteps, impAttrs, impPortraits, impVoices, impSkills, impProgress;
		std::string impQuestionText, impQuestionNo;
		std::vector<std::string> impText;
		std::vector<int> impAttrValues, impSkillPick;
		std::vector<Row> flowers, floristTowns, floristCards; int flowerSel = 0, flowerTown = 0; bool flowerNextDay = true;
		std::string flowerMessage, flowerName, flowerCost;
		std::vector<std::string> floristText, insuranceText, funeralText;
		std::vector<Row> insuranceRows;
	};
}
