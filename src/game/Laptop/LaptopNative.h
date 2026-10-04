#pragma once
// The game side of the native laptop (Phase 6, docs/ui/laptop.md): what the native screens read and the functions
// they call. Each function is implemented next to the legacy page it comes from, so it can use the same state and
// helpers; the native UI never re-implements a rule (hiring, prices, shipping, the I.M.P. profile, ...).

#include "JA2Types.h"
#include "ScreenIDs.h"

#include <string_theory/string>

#include <vector>

struct Email;

namespace LaptopNative
{
	// ---- shell (Laptop.cc) ------------------------------------------------------------------------------------
	/** The native laptop is open (the legacy laptop's frame code must not run; ExitLaptop does the native exit). */
	bool Active();
	/** Entering the laptop: pause, music, rain, bookmarks, per-site state: EnterLaptop without the drawing. */
	void Enter();
	/** Leaving: the legacy exit rules (the I.M.P. reminder mail, the first exit of a new game). Returns the screen
	 * to go to. */
	ScreenID Leave();
	/** The laptop's own exit (music, ambients, unpause), called from ExitLaptop. */
	void Exit();
	/** Sets guiCurrentLaptopMode (and the web mode) so that game code and automation see the page shown. */
	void SetMode(int laptopMode);
	int  Mode();
	/** Bookmarks set so far (AIM_BOOKMARK...), in the order the legacy list shows them. */
	std::vector<int> Bookmarks();
	/** Marks a site visited; true if this is the first visit this session (the legacy "World Wide Wait"). */
	bool VisitSite(int bookmark);
	bool IsRaining();
	bool MercSiteDown();
	bool BobbyRayOpen();

	// ---- e-mail (EMail.cc) ------------------------------------------------------------------------------------
	std::vector<Email*> Mails();
	ST::string MailSender(Email const&);
	/** Opens a mail as the legacy reader does (marks it read, special mails such as the I.M.P. results) and
	 * returns its paragraphs. */
	std::vector<ST::string> OpenMail(Email&);
	void DeleteMail(Email*);
	int  UnreadMails();

	// ---- files (Files.cc) -------------------------------------------------------------------------------------
	struct FileInfo { int index; ST::string title; bool read; };
	std::vector<FileInfo> Files();
	/** Marks the file read and returns its paragraphs (the first one is its title). */
	std::vector<ST::string> OpenFile(int index);
	int UnreadFiles();

	// ---- history (History.cc) ---------------------------------------------------------------------------------
	struct HistoryRow { UINT32 date; ST::string location, text; bool open; };
	std::vector<HistoryRow> History();

	// ---- finances (Finances.cc) -------------------------------------------------------------------------------
	struct FinanceRow { UINT32 date; ST::string text; INT32 amount, balance; };
	std::vector<FinanceRow> Transactions();
	struct FinanceSummary
	{
		INT32 yesterdayIncome, yesterdayOther, yesterdayDebits, yesterdayBalance;
		INT32 todayIncome, todayOther, todayDebits, balance, forecastIncome, projectedBalance;
	};
	FinanceSummary Summary();

	// ---- A.I.M. (AIMMembers.cc) -------------------------------------------------------------------------------
	enum class Contract { Day, Week, TwoWeeks };
	/** The total the legacy video conference charges: salary for @a length, medical deposit, gear. */
	INT32 AimContractCharge(ProfileID, Contract, bool equipment);
	/** What the merc says when called: true = will talk about a contract. @a quote is the quote they open with. */
	enum class Answer { Talks, AnsweringMachine, Unavailable };
	Answer AimCall(ProfileID, UINT16& quote);
	/** CanMercBeHired without the video face: false when they refuse; @a quote is what they say. */
	bool AimWillJoin(ProfileID, UINT16& quote);
	enum class HireResult { Hired, NoFunds, TeamFull, Failed };
	/** Hires as Transfer Funds does: contract, gear, medical deposit, finances, history. */
	HireResult AimHire(ProfileID, Contract, bool equipment);
	void AimLeaveMessage(ProfileID);
	/** A merc's line of speech as text, and starts its voice. */
	ST::string SayQuote(ProfileID, UINT16 quote);
	/** The A.I.M. bio paragraphs of a member (aimbios.edt). */
	std::vector<ST::string> AimBio(ProfileID);

	// ---- M.E.R.C. (Mercs*.cc) ---------------------------------------------------------------------------------
	/** The M.E.R.C. files that can be shown now (availability by day and spending). */
	std::vector<ProfileID> MercFiles();
	int  MercAccountStatus();
	void MercOpenAccount();
	/** Hires a M.E.R.C. merc as the files page does; false if not (away, team full, account suspended). */
	bool MercHire(ProfileID);
	UINT32 MercOwed();
	bool MercPay(); // false: not enough money
	/** What Speck says on the home page now (quote id, text, starts the voice). */
	ST::string SpeckSays();
	std::vector<ST::string> MercBio(ProfileID);

	// ---- Bobby Ray's (BobbyR*.cc) -----------------------------------------------------------------------------
	enum class Shop { Guns, Ammo, Armour, Misc, Used };
	struct ShopItem { UINT16 slot; UINT16 item; int onHand, onOrder, inCart, quality, price; };
	std::vector<ShopItem> ShopItems(Shop);
	/** +1 / -1 in the order, with the legacy checks (stock, 10 lines); a non-empty string is the refusal. */
	ST::string ShopAdd(Shop, UINT16 slot);
	void ShopRemove(Shop, UINT16 slot);
	ST::string ShopDescription(UINT16 item);
	struct OrderLine { int index; UINT16 item; int qty, unitPrice, total; bool used; int quality; UINT32 weight; };
	std::vector<OrderLine> OrderLines();
	void OrderClear();
	void OrderRemoveLine(int index);
	void OrderChange(int index, int delta);
	struct Destination { int id; ST::string name; bool canDeliver; };
	std::vector<Destination> Destinations();
	/** Shipping for @a speed (0 overnight, 1 two days, 2 standard) to @a city, and the totals. */
	struct OrderTotals { UINT32 weight; INT32 subtotal, shipping, total; INT32 ratePerKg[3]; };
	OrderTotals OrderTotal(int city, int speed);
	/** Accept Order asks "send this order to <city>?" unless it goes to the primary destination the player holds. */
	bool OrderNeedsConfirmation(int city);
	enum class OrderResult { Placed, NoFunds, NothingToOrder, NoDestination };
	OrderResult PlaceOrder(int city, int speed);
	struct Shipment { int index; UINT32 day; ST::string destination; int items; UINT32 weight; };
	std::vector<Shipment> Shipments();

	// ---- I.M.P. (IMP_*.cc) ------------------------------------------------------------------------------------
	bool ImpCanCreate();
	bool ImpCodeValid(ST::string const&);
	int  ImpCost();
	struct ImpQuestion { ST::string text; std::vector<ST::string> answers; };
	std::vector<ImpQuestion> ImpQuiz(bool female);
	struct ImpAttributes { int health, agility, dexterity, strength, leadership, wisdom, marksmanship, mechanical, explosives, medical; };
	int ImpAttributeMin();
	int ImpAttributeMax();
	int ImpBonusPoints();
	int ImpAttributeZeroValue(); // what a skill dropped to 0 gives back
	bool ImpPicksSkillsDirectly();
	struct ImpPortrait { int index; int face; bool female; };
	std::vector<ImpPortrait> ImpPortraits(bool female);
	struct ImpVoice { int index; ProfileID profile; bool female; };
	std::vector<ImpVoice> ImpVoices(bool female);
	void ImpPlayVoice(int voiceIndex);
	struct ImpChoices
	{
		ST::string fullName, nickName;
		bool female = false;
		std::vector<int> answers; // per question, -1 unanswered
		int attrs[10] = {}; // health, agility, dexterity, strength, leadership, wisdom, marksmanship, medical, mechanical, explosives
		int portrait = 0, voice = 0;
		std::vector<int> skills; // skill trait ids
	};
	/** Creates the character exactly as the legacy Finish/Confirm does: pays, compiles the profile, hires. */
	enum class ImpResult { Created, NoFunds, Invalid };
	ImpResult ImpCreate(ImpChoices const&);
	std::vector<ST::string> ImpSkillNames();
	/** I.M.P. text records: 0 home, 1 about us, 2 main page, 3 begin, 4 personality, 5 attributes, 6 portrait, 7 voice, 8 finish. */
	std::vector<ST::string> ImpPageText(int page);

	// ---- florist, insurance, mortuary -------------------------------------------------------------------------
	struct Flower { int index; ST::string name, price, description; INT32 cost; };
	std::vector<Flower> Flowers();
	std::vector<ST::string> FloristCards();
	struct FlowerOrder { int flower; int town; bool nextDay; ST::string message, name; };
	INT32 FlowerOrderCost(FlowerOrder const&);
	bool  SendFlowers(FlowerOrder const&); // false: not enough money
	std::vector<ST::string> FloristTowns();

	struct InsuranceRow { ProfileID profile; ST::string name; int daysLeft; bool insured, canInsure; INT32 premium, refund; };
	std::vector<InsuranceRow> InsuranceRows();
	bool Insure(ProfileID);   // false: not enough money / not allowed
	void CancelInsurance(ProfileID); // the legacy contract page has no cancel: a no-op kept for symmetry
	std::vector<ST::string> InsuranceText(int page); // 0 home, 1 info, 2 comments, 3 contract intro
	std::vector<ST::string> FuneralText();
	std::vector<ST::string> AimText(int page); // policies, history, links
}
