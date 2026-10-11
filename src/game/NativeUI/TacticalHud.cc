// The native tactical HUD (Phase 5, docs/ui/tactical.md): the squad bar, the merc detail panel with the inventory,
// the item description, the message lines and log, the turn banner, the sector card and the names over the mercs.
//
// It is an overlay over GAME_SCREEN, not a screen: the legacy tactical screen keeps running underneath with its own
// logic, panels and regions, and draws nothing of what the native HUD shows (Interface_Control.cc, Message.cc,
// Interface.cc check TacticalHudActive). The view model reads the game every frame. Its commands act through the
// legacy code: buttons press the legacy hotkeys (same handler, same rules), inventory slots and the description's
// attachments, unload and money buttons click the legacy regions and buttons of the hidden panel
// (Interface_Items.cc). The menus and popups (action, door, pick-up, stack, key ring, talk, sector exit, the speaking
// face) are models (PopupModels.h) that this view only draws: their choices come back as calls (PopupAdapter.h).
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "Animation_Control.h"
#include "ContentManager.h"
#include "Game_Clock.h"
#include "GameInstance.h"
#include "Handle_UI.h"
#include "English.h"
#include "Input.h"
#include "Timer_Control.h"
#include "Interface.h"
#include "Interface_Dialogue.h"
#include "Interface_Items.h"
#include "Interface_Panels.h"
#include "InventoryAdapter.h"
#include "PopupAdapter.h"
#include "Strategic_Exit_GUI.h"
#include "Tactical_Placement_GUI.h"
#include "ItemModel.h"
#include "Items.h"
#include "JAScreens.h"
#include "Keys.h"
#include "Logger.h"
#include "MagazineModel.h"
#include "Map_Screen_Interface.h"
#include "Finances.h"
#include "GameSettings.h"
#include "Isometric_Utils.h"
#include "WeaponModels.h"
#include "MercProfile.h"
#include "Drugs_And_Alcohol.h"
#include "Faces.h"
#include "LaptopSave.h"
#include "RenderWorld.h"
#include "Message.h"
#include "Overhead.h"
#include "Overhead_Map.h"
#include "Soldier_Control.h"
#include "Soldier_Macros.h"
#include "Soldier_Profile.h"
#include "Squads.h"
#include "StrategicMap.h"
#include "Text.h"
#include "UILayout.h"
#include "Video.h"
#include "Weapons.h"

#include <string_theory/format>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

extern ScreenID guiCurrentScreen;
extern OBJECTTYPE* gpItemDescObject;

namespace NativeUI
{

namespace
{
	std::string S(ST::string const& s) { return s.to_std_string(); }

	/** Pictures at an integer scale k = floor(base * dp) (at least 1), never stretched (MockWorld.cc). */
	struct Pic { std::string src; int w = 0, h = 0; };
	Pic MakePic(std::string const& name, float const base)
	{
		Pic p;
		auto const [w, h] = PictureBaseSize(name);
		if (!w) return p;
		int const k = std::max(1, int(std::floor(base * std::max(0.01f, DpScale()) + 0.001f)));
		p.src = name + "@" + std::to_string(k);
		p.w = w * k;
		p.h = h * k;
		return p;
	}
	/** A picture no bigger than maxW x maxH output pixels: the largest integer scale that fits (at least 1x). */
	Pic FitPic(std::string const& name, float const base, float const maxW, float const maxH)
	{
		auto const [w, h] = PictureBaseSize(name);
		if (!w) return {};
		float const dp = std::max(0.01f, DpScale());
		int k = std::max(1, int(std::floor(base * dp + 0.001f)));
		while (k > 1 && (w * k > maxW * dp || h * k > maxH * dp)) --k;
		Pic p;
		p.src = name + "@" + std::to_string(k);
		p.w = w * k;
		p.h = h * k;
		return p;
	}

	/** Keeps a popup of about wDp x hDp at (x, y) inside the view. */
	void ClampPopup(float& x, float& y, float const wDp, float const hDp)
	{
		float const dp = std::max(0.01f, DpScale());
		Rml::Vector2i const dim = Context()->GetDimensions();
		x = std::clamp(x, 0.f, std::max(0.f, float(dim.x) - wDp * dp));
		y = std::clamp(y, 0.f, std::max(0.f, float(dim.y) - hDp * dp));
	}

	/** Presses a legacy hotkey (down and up), as the keyboard would: the legacy handler runs with all its checks. */
	void PressKey(SDL_Keycode const key, SDL_Keymod const mods = SDL_KMOD_NONE)
	{
		auto send = [](SDL_Keycode const k, SDL_Keymod const m, bool const down) {
			SDL_KeyboardEvent e{};
			e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
			e.key  = k;
			e.mod  = m;
			e.down = down;
			if (down) KeyDown(&e); else KeyUp(&e);
		};
		if (mods & SDL_KMOD_SHIFT) send(SDLK_LSHIFT, mods, true);
		if (mods & SDL_KMOD_CTRL)  send(SDLK_LCTRL, mods, true);
		send(key, mods, true);
		send(key, mods, false);
		if (mods & SDL_KMOD_CTRL)  send(SDLK_LCTRL, SDL_KMOD_NONE, false);
		if (mods & SDL_KMOD_SHIFT) send(SDLK_LSHIFT, SDL_KMOD_NONE, false);
	}

	/** The second command argument is the mouse button (RmlUi: 0 left, 1 right). */
	bool Right(Args const& a) { return a.size() > 1 && std::atoi(a[1].c_str()) == 1; }

	std::string Stance(SOLDIERTYPE const& s)
	{
		switch (gAnimControl[s.usAnimState].ubEndHeight)
		{
			case ANIM_PRONE:  return "prone";
			case ANIM_CROUCH: return "crouch";
			default:          return "stand";
		}
	}

	std::string MessageClass(UINT16 const colour)
	{
		switch (colour)
		{
			case FONT_MCOLOR_RED:      return "combat";
			case FONT_WHITE:           return "dialogue";
			case FONT_MCOLOR_LTGREEN:  return "ok";
			default:                   return "";
		}
	}

	// ------------------------------------------------------------------ rows
	struct CardRow
	{
		int slot = 0;
		std::string name, face, sub, hand, ammo, handName;
		int fw = 0, fh = 0, hw = 0, hh = 0;
		int hp = 0, hpw = 0, lostw = 0, en = 0, mo = 0, ap = 0;
		bool empty = true, sel = false, done = false, crit = false, talking = false, low = false;
		bool bleeding = false, asleep = false, stealth = false, drunk = false, dead = false, vehicle = false;
		std::string drop; // while something is held: "ok" when it can be handed to this merc, "no" when not
		static void Describe(RowFields<CardRow>& f)
		{
			f("slot", &CardRow::slot)("name", &CardRow::name)("face", &CardRow::face)("sub", &CardRow::sub)
			 ("hand", &CardRow::hand)("ammo", &CardRow::ammo)("hand_name", &CardRow::handName)
			 ("fw", &CardRow::fw)("fh", &CardRow::fh)("hw", &CardRow::hw)("hh", &CardRow::hh)
			 ("hp", &CardRow::hp)("hpw", &CardRow::hpw)("lostw", &CardRow::lostw)("en", &CardRow::en)("mo", &CardRow::mo)
			 ("ap", &CardRow::ap)("empty", &CardRow::empty)("sel", &CardRow::sel)("done", &CardRow::done)
			 ("crit", &CardRow::crit)("talking", &CardRow::talking)("low", &CardRow::low)("bleeding", &CardRow::bleeding)
			 ("asleep", &CardRow::asleep)("stealth", &CardRow::stealth)("drunk", &CardRow::drunk)("dead", &CardRow::dead)
			 ("vehicle", &CardRow::vehicle)("drop", &CardRow::drop);
		}
	};

	struct SquadRow
	{
		int n = 0, count = 0;
		bool on = false;
		static void Describe(RowFields<SquadRow>& f) { f("n", &SquadRow::n)("count", &SquadRow::count)("on", &SquadRow::on); }
	};

	struct LineRow
	{
		std::string text, cls;
		static void Describe(RowFields<LineRow>& f) { f("text", &LineRow::text)("cls", &LineRow::cls); }
	};

	struct OverRow
	{
		std::string name, action, health, dmg;
		int x = 0, y = 0, hpw = 0, enw = 0;
		bool sel = false, bars = false, yellow = false;
		static void Describe(RowFields<OverRow>& f)
		{
			f("name", &OverRow::name)("action", &OverRow::action)("health", &OverRow::health)("dmg", &OverRow::dmg)
			 ("x", &OverRow::x)("y", &OverRow::y)("hpw", &OverRow::hpw)("enw", &OverRow::enw)("sel", &OverRow::sel)
			 ("bars", &OverRow::bars)("yellow", &OverRow::yellow);
		}
	};

	struct SlotRow
	{
		int idx = 0, w = 0, h = 0, cond = 0;
		std::string src, count, ghost, label, title;
		// the hover card: weight, the magazine in a gun, and one line per attachment (RML, escaped)
		std::string wt, ammo, atts;
		int natts = 0;
		bool empty = true, big = false, att = false, worn = false;
		std::string drop; // while something is held: "ok" when the pocket takes it, "no" when it does not
		static void Describe(RowFields<SlotRow>& f)
		{
			f("idx", &SlotRow::idx)("w", &SlotRow::w)("h", &SlotRow::h)("cond", &SlotRow::cond)("src", &SlotRow::src)
			 ("count", &SlotRow::count)("ghost", &SlotRow::ghost)("label", &SlotRow::label)("title", &SlotRow::title)
			 ("empty", &SlotRow::empty)("big", &SlotRow::big)("att", &SlotRow::att)("worn", &SlotRow::worn)
			 ("wt", &SlotRow::wt)("ammo", &SlotRow::ammo)("atts", &SlotRow::atts)("natts", &SlotRow::natts)("drop", &SlotRow::drop);
		}
	};

	struct KvRow
	{
		std::string k, v;
		static void Describe(RowFields<KvRow>& f) { f("k", &KvRow::k)("v", &KvRow::v); }
	};

	/** One line of the item sheet's specs: an icon, the value, and a bar (percent of a typical range) so two items
	 * read at a glance; `low` marks a stat where less is better (AP costs). */
	struct StatRow
	{
		std::string k, v, icon;
		int w = 0;
		bool low = false;
		static void Describe(RowFields<StatRow>& f) { f("k", &StatRow::k)("v", &StatRow::v)("icon", &StatRow::icon)("w", &StatRow::w)("low", &StatRow::low); }
	};

	/** One attribute in the detail panel: its value, a bar width (percent of the attribute's range) and whether it
	 * opens a group (physical, mental, skills). */
	struct AttrRow
	{
		std::string k, v;
		int w = 0;
		bool lead = false;
		static void Describe(RowFields<AttrRow>& f) { f("k", &AttrRow::k)("v", &AttrRow::v)("w", &AttrRow::w)("lead", &AttrRow::lead); }
	};

	/** One row of the action or door menu (Interface.h: NativeMenuView): item, group heading or separator. */
	struct MenuItemRow
	{
		int id = -1, ap = -1;
		std::string eid, kind, label, kbd, icon, title, why;
		bool disabled = false;
		static void Describe(RowFields<MenuItemRow>& f)
		{
			f("id", &MenuItemRow::id)("ap", &MenuItemRow::ap)("eid", &MenuItemRow::eid)("kind", &MenuItemRow::kind)
			 ("label", &MenuItemRow::label)("kbd", &MenuItemRow::kbd)("icon", &MenuItemRow::icon)
			 ("title", &MenuItemRow::title)("why", &MenuItemRow::why)("disabled", &MenuItemRow::disabled);
		}
	};

	/** One row of the pick-up menu (Interface_Items.h: NativePickupView). */
	struct PickRow
	{
		int slot = -1, item = 0, w = 0, h = 0, cond = 0;
		std::string eid, name, count, title, src;
		bool empty = true, sel = false, att = false;
		static void Describe(RowFields<PickRow>& f)
		{
			f("slot", &PickRow::slot)("item", &PickRow::item)("w", &PickRow::w)("h", &PickRow::h)("cond", &PickRow::cond)
			 ("eid", &PickRow::eid)("name", &PickRow::name)("count", &PickRow::count)("title", &PickRow::title)
			 ("src", &PickRow::src)("empty", &PickRow::empty)("sel", &PickRow::sel)("att", &PickRow::att);
		}
	};

	/** One box of the stack popup. */
	struct StkRow
	{
		int i = 0, w = 0, h = 0;
		std::string eid, text, src;
		bool filled = false, low = false;
		static void Describe(RowFields<StkRow>& f)
		{
			f("i", &StkRow::i)("w", &StkRow::w)("h", &StkRow::h)("eid", &StkRow::eid)("text", &StkRow::text)
			 ("src", &StkRow::src)("filled", &StkRow::filled)("low", &StkRow::low);
		}
	};

	/** One key of the key ring. */
	struct KeyRow
	{
		int slot = 0, count = 0, w = 0, h = 0;
		std::string eid, use_id, take_id, name, found, src, why;
		bool can_use = false, fits = false;
		static void Describe(RowFields<KeyRow>& f)
		{
			f("slot", &KeyRow::slot)("count", &KeyRow::count)("w", &KeyRow::w)("h", &KeyRow::h)("eid", &KeyRow::eid)
			 ("use_id", &KeyRow::use_id)("take_id", &KeyRow::take_id)("name", &KeyRow::name)("found", &KeyRow::found)
			 ("src", &KeyRow::src)("why", &KeyRow::why)("can_use", &KeyRow::can_use)("fits", &KeyRow::fits);
		}
	};

	/** One approach of the talk panel. */
	struct TalkRowV
	{
		int approach = 0;
		std::string eid, label, key, title, why;
		bool enabled = true;
		static void Describe(RowFields<TalkRowV>& f)
		{
			f("approach", &TalkRowV::approach)("eid", &TalkRowV::eid)("label", &TalkRowV::label)("key", &TalkRowV::key)
			 ("title", &TalkRowV::title)("why", &TalkRowV::why)("enabled", &TalkRowV::enabled);
		}
	};

	// ------------------------------------------------------------------ the view model
	// a drop on a squad card happened this click: the click that follows is not a selection
	bool g_swallowClick = false;
	bool g_swallowUp = false; // a cancelled drag: the button's release is not a click on the pocket under it
	bool g_escWasDown = false;
	bool g_releaseSeen = false;
	bool g_rightWasDown = false;
	bool g_dragLifted = false; // a drag lifted the item and the button is still down
	int g_downCard = -1; // the squad card the left button went down on, -1 none
	// the press on a pocket that may become a drag
	Equipment::DragGesture g_gesture;

	class TacticalViewModel final : public ViewModel
	{
	public:
		// squad bar
		std::vector<CardRow> cards;
		std::vector<SquadRow> squads;
		bool combat = false, ourTurn = false, canEnd = false, burst = false, run = false, stealth = false, roof = false;
		std::string stance;
		// banner, sector card, messages
		bool banner = false;
		std::string bannerCls, bannerTitle, bannerText;
		int bannerPct = 0;
		bool bannerProgress = false;
		std::string sector, town, day, clock;
		std::vector<LineRow> lines;
		bool logOpen = false;
		std::string logFilter = "all";
		std::vector<LineRow> log;
		int logAll = 0, logCombat = 0, logSpeech = 0, logSystem = 0;
		std::vector<OverRow> overlays;
		// detail panel
		bool detail = false;
		bool dMute = false;
		std::string dDoll = "gen-doll", dName, dFull, dFace, dVitHp, dEn, dMo, dMoney, dKeys, dWeight, dCamo, dArmour;
		int dFw = 0, dFh = 0, dHpw = 0, dLostw = 0, dEnw = 0, dMow = 0, dWeightw = 0, dCamow = 0, dArmourw = 0;
		bool dHeavy = false;
		std::vector<AttrRow> attrs;
		std::vector<SlotRow> body, hands, lbe, bigPockets, smallPockets, beltPockets, packPockets;
		// item description
		bool desc = false, descMoney = false, descGun = false, descWeapon = false, descProsCons = false, descHatched = false;
		std::string xName, xType, xText, xPic, xStatusLabel, xStatus, xWeight, xPros, xCons, xAmmo, xAmmoType, xKey;
		int xPw = 0, xPh = 0, xCond = 0;
		std::vector<StatRow> xStats;
		std::string xTab = "general";
		std::vector<SlotRow> xAtts;
		SlotRow xMag;
		std::string mTotal, mRemaining, mRemoving;
		bool m1000 = false, m100 = false, m10 = false;
		// the question a move asked (the core holds it), and the line the last refusal gave
		bool ask = false;
		std::string askText, hint, lYes, lNo;
		unsigned hintSeq = 0;
		UINT32 hintAt = 0;
		// action and door menus (right click hold, or clicking a door)
		bool menuOpen = false;
		std::string menuTitle, menuSub;
		int menuX = 0, menuY = 0;
		std::vector<MenuItemRow> menu;
		// pick-up menu (items on the ground)
		bool pickOpen = false;
		std::string pickTitle, pickSub, pickOk;
		int pickX = 0, pickY = 0, pickPage = 0, pickPages = 0;
		bool pickCanUp = false, pickCanDown = false, pickAll = false, pickEnabled = false;
		std::vector<PickRow> pick;
		// stack popup
		bool stkOpen = false, stkHolding = false, stkMore = false, stkLess = false;
		std::string stkName, stkWhere, stkTakeLabel, stkHint;
		int stkX = 0, stkY = 0, stkCount = 0, stkTake = 0, stkSlot = -1;
		std::vector<StkRow> stk;
		// key ring
		bool keyOpen = false, keyDoor = false, keyNone = false, keyHolding = false;
		std::string keyWho, keyHint;
		int keyX = 0, keyY = 0;
		std::vector<KeyRow> keys;
		// talk panel
		bool talkOpen = false, talkSpeaking = false;
		std::string talkName, talkLine, talkPrev, talkFace;
		int talkFw = 0, talkFh = 0, talkBottom = 0;
		std::vector<TalkRowV> talk;
		// the speaking face and its subtitle
		bool spOpen = false, spBubble = false;
		std::string spWho, spLine, spFace;
		int spFw = 0, spFh = 0, spX = 0, spY = 0;
		// sector exit
		bool exOpen = false, exSingleSel = false, exAllSel = false, exLoadSel = false;
		bool exSingleOff = false, exAllOff = false, exLoadOff = false, exCanGo = false;
		std::string exDir, exDirLabel, exTitle, exSingleLabel, exAllLabel, exLoadLabel, exSingleCount, exAllCount;
		std::string exSingleTip, exAllTip, exLoadTip, exSingleWhy, exAllWhy, exLoadWhy, exTrip;
		// labels
		std::string lTake, lTakeAll, lKeyRing, lUse, lGive, lNoKeys, lClose, lTalkWho, lTravel, lCancel, lGo, lPutKey, lExitNone;
		std::string lEndTurn, lTurnBased, lMap, lDone, lUnload, lPros, lCons, lAttachments, lAmmo, lWeapon, lLog, lLoadout;

		std::string signature;

		TacticalViewModel() : ViewModel("tactical", TOPIC_ALL)
		{
			Command("select", [](Args const& a) {
				if (a.empty()) return;
				// the drop on this card already happened: the click that follows it is not a selection
				if (g_swallowClick) { g_swallowClick = false; return; }
				int const slot = std::atoi(a[0].c_str());
				if (slot >= 0 && slot < 10) PressKey(SDL_Keycode(SDLK_F1 + slot));
			});
			Command("details", [](Args const& a) {
				if (a.empty()) return;
				int const slot = std::atoi(a[0].c_str());
				SOLDIERTYPE* const s = slot >= 0 && slot < NUM_TEAM_SLOTS ? GetPlayerFromInterfaceTeamSlot(UINT8(slot)) : nullptr;
				if (!s) return;
				if (GetSelectedMan() != s && slot < 10) PressKey(SDL_Keycode(SDLK_F1 + slot));
				if (gsCurInterfacePanel != SM_PANEL) PressKey(SDLK_GRAVE);
			});
			Command("squad", [](Args const& a) {
				if (a.empty()) return;
				int const n = std::atoi(a[0].c_str());
				if (n >= 1 && n <= 10) PressKey(SDL_Keycode(n == 10 ? SDLK_0 : SDLK_1 + (n - 1)));
			});
			Command("next_squad", [](Args const&) { PressKey(SDLK_SPACE, SDL_KMOD_LSHIFT); });
			// buttons: the legacy hotkey of each (Turn_Based_Input.cc)
			Command("key", [](Args const& a) {
				if (a.empty()) return;
				std::string const k = a[0];
				if (k == "tab")        PressKey(SDLK_TAB);
				else if (k == "grave") PressKey(SDLK_GRAVE);
				else if (k == "insert") PressKey(SDLK_INSERT);
				else if (k.size() == 1) PressKey(SDL_Keycode(k[0]));
			});
			Command("talk", [](Args const&) { ToggleTalkCursorMode(&guiCurrentEvent); });
			Command("options", [](Args const&) { PressKey(SDLK_O); });
			Command("overhead", [](Args const&) { if (!InOverheadMap()) GoIntoOverheadMap(); });
			Command("money_region", [](Args const&) { InventoryCashButton(); });
			Command("keyring", [](Args const&) { if (InKeyRingPopup()) KeyRingClose(); else InventoryKeyRing(); });
			// slot(index, mouse button): left picks up or puts down, right shows the description (the inventory core decides)
			Command("slot", [](Args const& a) {
				if (g_swallowUp) { g_swallowUp = false; return; }
				if (!a.empty()) InventorySlotClick(gpSMCurrentMerc, std::atoi(a[0].c_str()), Right(a), _KeyDown(CTRL));
			});
			// card_drop(slot): the hand is let go over a squad card - give, or on his own card drop at his feet
			Command("card_drop", [](Args const& a) {
				if (a.empty() || InventoryHand().item == NOTHING) return;
				int const slot = std::atoi(a[0].c_str());
				SOLDIERTYPE* const s = slot >= 0 && slot < NUM_TEAM_SLOTS ? GetPlayerFromInterfaceTeamSlot(UINT8(slot)) : nullptr;
				if (!s) return;
				DropOnCard(s);
				// a click-to-pick then click on the card ends in a click event too; a drag from a pocket does not
				g_swallowClick = g_downCard == slot;
			});
			Command("detail_close", [](Args const&) { if (gsCurInterfacePanel == SM_PANEL) PressKey(SDLK_GRAVE); });
			Command("prev_merc", [](Args const&) { PressKey(SDLK_SPACE); });
			Command("att", [](Args const& a) { if (!a.empty()) InventoryAttachClick(std::atoi(a[0].c_str()), Right(a)); });
			Command("unload", [](Args const&) { InventoryUnload(); });
			Command("desc_done", [](Args const&) { ItemDescNativeClose(); });
			// the question a move asked (merge two items; mount an attachment that cannot come off again)
			Command("ask_yes", [](Args const&) { InventoryAnswer(true); });
			Command("ask_no", [](Args const&) { InventoryAnswer(false); });
			Command("money", [](Args const& a) { if (!a.empty()) InventoryMoneyStep(std::atoi(a[0].c_str()), Right(a)); });
			Command("log", [this](Args const&) { logOpen = !logOpen; if (logOpen) ReadLog(); Changed(); });
			Command("log_filter", [this](Args const& a) { logFilter = a.empty() ? "all" : a[0]; ReadLog(); Changed(); });
			// the item sheet's page: "general" (specs, ammo, attachments) or "desc" (the dossier text, pros and cons)
			Command("desc_tab", [this](Args const& a) { xTab = a.empty() ? "general" : a[0]; Changed(); });
			// the menus and popups: each choice is a call into the popup's adapter (PopupAdapter.h)
			Command("menu_item", [](Args const& a) { if (!a.empty()) PopupMenuChoose(std::atoi(a[0].c_str())); });
			Command("menu_cancel", [](Args const&) { PopupMenuCancel(); });
			Command("pick_item", [](Args const& a) { if (!a.empty()) PickupToggle(std::atoi(a[0].c_str())); });
			Command("pick_hover", [](Args const& a) { PickupHover(a.empty() ? -1 : std::atoi(a[0].c_str())); });
			Command("pick_all", [](Args const&) { PickupAll(); });
			Command("pick_ok", [](Args const&) { PickupTake(); });
			Command("pick_cancel", [](Args const&) { PickupCancel(); });
			Command("pick_scroll", [](Args const& a) { if (!a.empty()) PickupScroll(std::atoi(a[0].c_str())); });
			// a box of the stack popup: left takes that object (or puts the hand's), right describes it
			Command("stack_box", [](Args const& a) {
				if (a.empty()) return;
				int const i = std::atoi(a[0].c_str());
				if (Right(a)) StackDescribe(i); else StackClickBox(i);
			});
			Command("stack_more", [](Args const&) { StackSplitStep(1); });
			Command("stack_less", [](Args const&) { StackSplitStep(-1); });
			Command("stack_take", [](Args const&) { StackTakeSplit(); });
			Command("stack_all", [](Args const&) { StackTakeAll(); });
			Command("stack_close", [](Args const&) { StackClose(); });
			// a key row: left on the row describes nothing; the buttons use it or take it into the hand, right describes it
			Command("key_row", [](Args const& a) { if (!a.empty() && Right(a)) KeyRingDescribe(std::atoi(a[0].c_str())); });
			Command("key_use", [](Args const& a) { if (!a.empty()) KeyRingUse(std::atoi(a[0].c_str())); });
			Command("key_take", [](Args const& a) { if (!a.empty()) KeyRingTake(std::atoi(a[0].c_str())); });
			Command("key_close", [](Args const&) { KeyRingClose(); });
			Command("key_put", [](Args const&) { KeyRingPut(); });
			Command("talk_choose", [](Args const& a) { if (!a.empty()) TalkChoose(std::atoi(a[0].c_str())); });
			Command("talk_who", [](Args const&) { TalkWho(); });
			Command("talk_done", [](Args const&) { TalkDone(); });
			Command("speech_click", [](Args const&) { SpeechClick(); });
			Command("exit_single", [](Args const&) { ExitChooseSingle(); });
			Command("exit_all", [](Args const&) { ExitChooseAll(); });
			Command("exit_load", [](Args const&) { ExitToggleLoad(); });
			Command("exit_go", [](Args const&) { ExitGo(); });
			Command("exit_cancel", [](Args const&) { ExitCancel(); });
			Command("mute", [](Args const&) { NativeSMMuteClick(); });
			Command("swap_hands", [](Args const&) { PressKey(SDLK_Q, SDL_KMOD_CTRL); });
			Command("readout", [](Args const&) { OpenWeaponReadout(); });
			Command("loadout", [](Args const&) { OpenLoadout(); });

			lEndTurn = Str("tac.end_turn");
			lTurnBased = Str("tac.turn_based");
			lMap = Str("tac.map");
			lDone = Str("tac.done");
			lUnload = Str("tac.unload");
			lPros = S(gzProsLabel);
			lCons = S(gzConsLabel);
			lAttachments = Str("tac.attachments");
			lAmmo = Str("tac.ammo");
			lWeapon = Str("tac.weapon");
			lLog = Str("tac.log");
			lLoadout = Str("tac.loadout");
			lYes = Str("tac.yes");
			lTake = Str("tac.stack.take");
			lTakeAll = Str("tac.stack.all");
			lKeyRing = Str("tac.key.title");
			lUse = Str("tac.key.use");
			lGive = Str("tac.key.give");
			lNoKeys = Str("tac.key.none");
			lClose = Str("tac.close");
			lTalkWho = Str("tac.talk.who");
			lTravel = Str("tac.exit.travel");
			lCancel = Str("tac.pick.cancel");
			lGo = Str("tac.exit.go");
			lPutKey = Str("tac.key.put");
			lExitNone = Str("tac.exit.none");
			lNo = Str("tac.no");
		}

		void Describe(Fields& f) override
		{
			f.Rows("cards", cards);
			f.Rows("squads", squads);
			f.Field("combat", combat); f.Field("our_turn", ourTurn); f.Field("can_end", canEnd);
			f.Field("burst", burst); f.Field("run", run); f.Field("stealth", stealth); f.Field("roof", roof);
			f.Field("stance", stance);
			f.Field("banner", banner); f.Field("banner_cls", bannerCls); f.Field("banner_title", bannerTitle);
			f.Field("banner_text", bannerText); f.Field("banner_pct", bannerPct); f.Field("banner_progress", bannerProgress);
			f.Field("sector", sector); f.Field("town", town); f.Field("day", day); f.Field("clock", clock);
			f.Rows("lines", lines);
			f.Field("log_open", logOpen); f.Field("log_filter", logFilter); f.Rows("log", log);
			f.Field("log_all", logAll); f.Field("log_combat", logCombat); f.Field("log_speech", logSpeech); f.Field("log_system", logSystem);
			f.Rows("overlays", overlays);
			f.Field("detail", detail);
			f.Field("d_mute", dMute);
			f.Field("d_doll", dDoll); f.Field("d_name", dName); f.Field("d_full", dFull); f.Field("d_face", dFace); f.Field("d_fw", dFw); f.Field("d_fh", dFh);
			f.Field("d_hp", dVitHp); f.Field("d_en", dEn); f.Field("d_mo", dMo);
			f.Field("d_hpw", dHpw); f.Field("d_lostw", dLostw); f.Field("d_enw", dEnw); f.Field("d_mow", dMow);
			f.Field("d_money", dMoney); f.Field("d_keys", dKeys); f.Field("d_weight", dWeight); f.Field("d_camo", dCamo); f.Field("d_armour", dArmour);
			f.Field("d_weightw", dWeightw); f.Field("d_camow", dCamow); f.Field("d_armourw", dArmourw); f.Field("d_heavy", dHeavy);
			f.Rows("attrs", attrs);
			f.Rows("body", body); f.Rows("hands", hands); f.Rows("lbe", lbe); f.Rows("big", bigPockets); f.Rows("small", smallPockets);
			f.Rows("belt", beltPockets); f.Rows("pack", packPockets);
			f.Field("desc", desc); f.Field("desc_money", descMoney); f.Field("desc_gun", descGun); f.Field("desc_weapon", descWeapon);
			f.Field("desc_pros_cons", descProsCons); f.Field("desc_hatched", descHatched);
			f.Field("x_name", xName); f.Field("x_type", xType); f.Field("x_text", xText); f.Field("x_pic", xPic);
			f.Field("x_pw", xPw); f.Field("x_ph", xPh); f.Field("x_status_label", xStatusLabel); f.Field("x_status", xStatus);
			f.Field("x_cond", xCond); f.Field("x_weight", xWeight); f.Field("x_pros", xPros); f.Field("x_cons", xCons);
			f.Field("x_ammo", xAmmo); f.Field("x_ammo_type", xAmmoType); f.Field("x_key", xKey);
			f.Rows("x_stats", xStats); f.Field("x_tab", xTab); f.Rows("x_atts", xAtts);
			f.Field("m_total", mTotal); f.Field("m_remaining", mRemaining); f.Field("m_removing", mRemoving);
			f.Field("m_1000", m1000); f.Field("m_100", m100); f.Field("m_10", m10);
			f.Field("ask", ask); f.Field("ask_text", askText); f.Field("hint", hint);
			f.Field("l_yes", lYes); f.Field("l_no", lNo);
			f.Field("menu_open", menuOpen); f.Field("menu_title", menuTitle); f.Field("menu_sub", menuSub);
			f.Field("menu_x", menuX); f.Field("menu_y", menuY); f.Rows("menu", menu);
			f.Field("pick_open", pickOpen); f.Field("pick_title", pickTitle); f.Field("pick_sub", pickSub);
			f.Field("pick_ok", pickOk); f.Field("pick_x", pickX); f.Field("pick_y", pickY);
			f.Field("pick_page", pickPage); f.Field("pick_pages", pickPages);
			f.Field("pick_can_up", pickCanUp); f.Field("pick_can_down", pickCanDown); f.Field("pick_all", pickAll);
			f.Field("pick_ok_enabled", pickEnabled);
			f.Rows("pick", pick);
			f.Field("stk_open", stkOpen); f.Field("stk_name", stkName); f.Field("stk_where", stkWhere);
			f.Field("stk_x", stkX); f.Field("stk_y", stkY); f.Field("stk_count", stkCount); f.Field("stk_take", stkTake);
			f.Field("stk_holding", stkHolding); f.Field("stk_more", stkMore); f.Field("stk_less", stkLess);
			f.Field("stk_take_label", stkTakeLabel); f.Field("stk_hint", stkHint); f.Rows("stk", stk);
			f.Field("key_open", keyOpen); f.Field("key_who", keyWho); f.Field("key_hint", keyHint); f.Field("key_door", keyDoor);
			f.Field("key_none", keyNone); f.Field("key_holding", keyHolding); f.Field("key_x", keyX); f.Field("key_y", keyY); f.Rows("keys", keys);
			f.Field("talk_open", talkOpen); f.Field("talk_name", talkName); f.Field("talk_line", talkLine);
			f.Field("talk_prev", talkPrev); f.Field("talk_face", talkFace); f.Field("talk_fw", talkFw); f.Field("talk_fh", talkFh);
			f.Field("talk_speaking", talkSpeaking); f.Field("talk_bottom", talkBottom); f.Rows("talk", talk);
			f.Field("sp_open", spOpen); f.Field("sp_bubble", spBubble); f.Field("sp_who", spWho); f.Field("sp_line", spLine);
			f.Field("sp_face", spFace); f.Field("sp_fw", spFw); f.Field("sp_fh", spFh); f.Field("sp_x", spX); f.Field("sp_y", spY);
			f.Field("ex_open", exOpen); f.Field("ex_dir", exDir); f.Field("ex_dir_label", exDirLabel); f.Field("ex_title", exTitle);
			f.Field("ex_single_label", exSingleLabel); f.Field("ex_all_label", exAllLabel); f.Field("ex_load_label", exLoadLabel);
			f.Field("ex_single_count", exSingleCount); f.Field("ex_all_count", exAllCount);
			f.Field("ex_single_sel", exSingleSel); f.Field("ex_all_sel", exAllSel); f.Field("ex_load_sel", exLoadSel);
			f.Field("ex_single_off", exSingleOff); f.Field("ex_all_off", exAllOff); f.Field("ex_load_off", exLoadOff);
			f.Field("ex_single_tip", exSingleTip); f.Field("ex_all_tip", exAllTip); f.Field("ex_load_tip", exLoadTip);
			f.Field("ex_single_why", exSingleWhy); f.Field("ex_all_why", exAllWhy); f.Field("ex_load_why", exLoadWhy);
			f.Field("ex_trip", exTrip); f.Field("ex_can_go", exCanGo);
			f.Field("l_take", lTake); f.Field("l_take_all", lTakeAll); f.Field("l_key_ring", lKeyRing); f.Field("l_use", lUse);
			f.Field("l_give", lGive); f.Field("l_no_keys", lNoKeys); f.Field("l_close", lClose); f.Field("l_talk_who", lTalkWho);
			f.Field("l_travel", lTravel); f.Field("l_cancel", lCancel); f.Field("l_go", lGo); f.Field("l_put_key", lPutKey); f.Field("l_exit_none", lExitNone);
			f.Field("l_end_turn", lEndTurn); f.Field("l_turn_based", lTurnBased); f.Field("l_map", lMap); f.Field("l_done", lDone);
			f.Field("l_unload", lUnload); f.Field("l_pros", lPros); f.Field("l_cons", lCons); f.Field("l_attachments", lAttachments);
			f.Field("l_ammo", lAmmo); f.Field("l_weapon", lWeapon); f.Field("l_log", lLog); f.Field("l_loadout", lLoadout);
		}

		void Refresh() override
		{
			ReadSquad();
			ReadStatus();
			ReadMessages();
			ReadOverlays();
			ReadDetail();
			ReadDesc();
			ReadMenus();
			ReadPopups();
			ReadAsk();
			if (logOpen) ReadLog();
		}

		void ReadSquad()
		{
			cards.clear();
			combat = (gTacticalStatus.uiFlags & INCOMBAT) != 0;
			ourTurn = !combat || gTacticalStatus.ubCurrentTeam == OUR_TEAM;
			SOLDIERTYPE const* const sel = GetSelectedMan();
			int const n = std::min<int>(NUM_TEAM_SLOTS, 12);
			int filled = 0;
			for (int i = 0; i < n; ++i)
			{
				CardRow r;
				r.slot = i;
				SOLDIERTYPE const* const s = GetPlayerFromInterfaceTeamSlot(UINT8(i));
				if (s)
				{
					++filled;
					r.empty = false;
					r.name = S(s->name);
					Pic const f = MakePic("sface-" + std::to_string(s->ubProfile != NO_PROFILE ? GetProfile(s->ubProfile).ubFaceIndex : 0), 2);
					r.face = f.src; r.fw = f.w; r.fh = f.h;
					r.hp = s->bLife;
					r.hpw = std::clamp(int(s->bLife) * 100 / std::max<int>(1, s->bLifeMax), 0, 100);
					r.lostw = std::clamp((int(s->bLifeMax) - s->bLife) * 100 / std::max<int>(1, s->bLifeMax), 0, 100);
					r.en = std::clamp(int(s->bBreath), 0, 100);
					r.mo = std::clamp(int(s->bMorale), 0, 100);
					r.ap = s->bActionPoints;
					r.sel = s == sel;
					r.done = combat && s->bActionPoints <= 0;
					r.dead = s->bLife <= 0;
					r.crit = s->bLife > 0 && s->bLife < OKLIFE;
					r.bleeding = s->bBleeding > 0;
					r.asleep = s->fMercAsleep;
					r.stealth = s->bStealthMode;
					r.drunk = GetDrunkLevel(s) != SOBER;
					r.vehicle = (s->uiStatusFlags & SOLDIER_VEHICLE) != 0;
					r.talking = s->face && s->face->fTalking;
					if (InventoryHand().item != NOTHING) r.drop = PlanDropOnCard(s).Ok() ? "ok" : "no";
					// "|Stand/Walk", "|Crouch/Crouched Move", "Stand/|Run", "|Prone/Crawl": the stance is before the slash
					r.sub = S(pTacticalPopupButtonStrings[Stance(*s) == "prone" ? 3 : Stance(*s) == "crouch" ? 1 : 0]);
					r.sub.erase(std::remove(r.sub.begin(), r.sub.end(), '|'), r.sub.end());
					if (r.sub.find('/') != std::string::npos) r.sub = r.sub.substr(0, r.sub.find('/'));
					OBJECTTYPE const& hand = s->inv[HANDPOS];
					if (hand.usItem != NOTHING)
					{
						Pic const h = FitPic("nitem-" + std::to_string(hand.usItem), 1, 140, 26);
						r.hand = h.src; r.hw = h.w; r.hh = h.h;
						r.handName = S(GCM->getItem(hand.usItem)->getShortName());
						ItemModel const* const it = GCM->getItem(hand.usItem);
						if (it->isGun())
						{
							int const mag = GCM->getWeapon(hand.usItem)->ubMagSize;
							r.ammo = ST::format("{}/{}", hand.ubGunShotsLeft, mag).to_std_string();
							r.low = hand.ubGunShotsLeft * 4 < mag;
						}
						else if (hand.ubNumberOfObjects > 1)
						{
							r.ammo = ST::format("×{}", hand.ubNumberOfObjects).to_std_string();
						}
					}
				}
				cards.push_back(r);
			}
			// the bar shows at least six slots, and the empty ones beyond only up to the squad size
			while (int(cards.size()) > std::max(6, filled) && cards.back().empty) cards.pop_back();

			squads.clear();
			int const current = CurrentSquad();
			for (int q = 0; q < 10; ++q)
			{
				int const count = NumberOfPeopleInSquad(q);
				if (count == 0 && q != current) continue;
				SquadRow r;
				r.n = q + 1;
				r.count = count;
				r.on = q == current;
				squads.push_back(r);
			}

			if (sel)
			{
				stance = Stance(*sel);
				burst = sel->bDoBurst;
				run = sel->usUIMovementMode == RUNNING;
				stealth = sel->bStealthMode;
			}
			roof = gsInterfaceLevel != 0;
			canEnd = combat && gTacticalStatus.ubCurrentTeam == OUR_TEAM;
		}

		void ReadStatus()
		{
			TacticalStatusType const& ts = gTacticalStatus;
			banner = ts.fInTopMessage;
			bannerProgress = false;
			bannerPct = 0;
			bannerText.clear();
			if (banner)
			{
				switch (ts.ubTopMessageType)
				{
					case COMPUTER_TURN_MESSAGE:
						bannerCls = "foe";
						bannerTitle = S(ts.ubCurrentTeam == CREATURE_TEAM && HostileBloodcatsPresent() ? g_langRes->Message[STR_BLOODCATS_TURN] : TeamTurnString[ts.ubCurrentTeam]);
						bannerProgress = true;
						break;
					case AIR_RAID_TURN_MESSAGE:
						bannerCls = "foe";
						bannerTitle = S(TacticalStr[AIR_RAID_TURN_MESSAGE]);
						bannerProgress = true;
						break;
					case COMPUTER_INTERRUPT_MESSAGE:
					case MILITIA_INTERRUPT_MESSAGE:
						bannerCls = "int foe";
						bannerTitle = S(g_langRes->Message[STR_INTERRUPT]);
						bannerProgress = true;
						break;
					case PLAYER_INTERRUPT_MESSAGE:
						bannerCls = "int";
						bannerTitle = S(g_langRes->Message[STR_INTERRUPT]);
						break;
					default:
						bannerCls = "";
						bannerTitle = S(TeamTurnString[OUR_TEAM]);
						break;
				}
				if (ts.usTactialTurnLimitMax > 0)
				{
					bannerPct = std::clamp(int(ts.usTactialTurnLimitCounter) * 100 / int(ts.usTactialTurnLimitMax), 0, 100);
					if (gGameOptions.fTurnTimeLimit) bannerProgress = true;
				}
				if (bannerCls.empty() || bannerCls == "int")
				{
					int left = 0, total = 0;
					CFOR_EACH_IN_TEAM(s, OUR_TEAM)
					{
						if (!s->bInSector || s->bLife < OKLIFE || s->bAssignment != CurrentSquad()) continue;
						++total;
						if (s->bActionPoints > 0) ++left;
					}
					bannerText = ST::format(Str("tac.with_ap").c_str(), left, total).to_std_string();
				}
			}
			ST::string const id = GetSectorIDString(gWorldSector, TRUE);
			std::string full = S(id);
			auto const colon = full.find(':');
			sector = colon == std::string::npos ? full : full.substr(0, colon);
			town = colon == std::string::npos ? "" : full.substr(colon + 1);
			while (!town.empty() && town.front() == ' ') town.erase(0, 1);
			day = ST::format("{} {}", gpGameClockString, GetWorldDay()).to_std_string();
			UINT32 const m = GetWorldMinutesInDay();
			clock = ST::format("{02d}:{02d}", m / 60, m % 60).to_std_string();
		}

		void ReadMessages()
		{
			lines.clear();
			auto const l = GetTacticalScrollLines();
			for (auto it = l.rbegin(); it != l.rend(); ++it) lines.push_back({ S(it->text), MessageClass(it->colour) });
		}

		void ReadLog()
		{
			log.clear();
			logAll = logCombat = logSpeech = logSystem = 0;
			for (MessageLine const& m : GetMessageHistory())
			{
				std::string const cls = MessageClass(m.colour);
				++logAll;
				bool const c = cls == "combat", d = cls == "dialogue";
				if (c) ++logCombat; else if (d) ++logSpeech; else ++logSystem;
				if (logFilter == "combat" && !c) continue;
				if (logFilter == "speech" && !d) continue;
				if (logFilter == "system" && (c || d)) continue;
				log.push_back({ S(m.text), cls });
			}
		}

		void ReadOverlays()
		{
			overlays.clear();
			SOLDIERTYPE const* const sel = GetSelectedMan();
			FOR_EACH_MERC(i)
			{
				SOLDIERTYPE& s = **i;
				if (s.bVisible == -1 && !(gTacticalStatus.uiFlags & SHOW_ALL_MERCS)) continue;
				if (s.sGridNo == NOWHERE || !s.bInSector) continue;
				if (gAnimControl[s.usAnimState].uiFlags & ANIM_NOSHOW_MARKER) continue;
				if (s.uiStatusFlags & SOLDIER_DEAD) continue;
				bool const ours = s.bTeam == OUR_TEAM;
				bool const shown = &s == sel || s.fShowLocator || s.uiStatusFlags & SOLDIER_MULTI_SELECTED ||
					(&s == gSelectedGuy && !gfIgnoreOnSelectedGuy) || (ours && s.ubProfile != NO_PROFILE);
				bool const damage = s.fDisplayDamage;
				if (!shown && !damage) continue;
				INT16 x, y;
				GetSoldierAboveGuyPositions(&s, &x, &y, FALSE);
				OverRow r;
				// the canvas point centred over the merc (the legacy text box is 80 wide), where the native UI draws
				Rml::Vector2f const at = CanvasToOutput(float(x + 40), float(y));
				r.x = int(at.x);
				r.y = int(at.y);
				r.sel = &s == sel;
				if (damage) r.dmg = ST::format("−{}", s.sDamage).to_std_string();
				if (shown)
				{
					if (s.ubProfile != NO_PROFILE || s.uiStatusFlags & SOLDIER_VEHICLE)
					{
						r.name = S(s.name);
						if (&s == gUIValidCatcher && gfUIMouseOnValidCatcher == 1) r.action = S(TacticalStr[CATCH_STR]);
						else if (&s == gUIValidCatcher && gfUIMouseOnValidCatcher == 3) r.action = S(TacticalStr[RELOAD_STR]);
						else if (&s == gUIValidCatcher && gfUIMouseOnValidCatcher == 4) r.action = S(pMessageStrings[MSG_PASS]);
						else if (&s == gUIValidCatcher && gfUIMouseOnValidCatcher == 2) r.action = S(TacticalStr[GIVE_STR]);
						else if (s.bAssignment >= ON_DUTY) { r.action = ST::format("({})", pAssignmentStrings[s.bAssignment]).to_std_string(); r.yellow = true; }
						else if (ours && s.bAssignment < ON_DUTY && s.bAssignment != CurrentSquad() && !(s.uiStatusFlags & SOLDIER_MULTI_SELECTED))
							r.action = st_format_printf(gzLateLocalizedString[STR_LATE_34], s.bAssignment + 1).to_std_string();
						r.bars = (s.ubProfile != NO_PROFILE && MercProfile(s.ubProfile).isPlayerMerc()) || RPC_RECRUITED(&s) || AM_AN_EPC(&s) ||
							s.uiStatusFlags & SOLDIER_VEHICLE;
						if (!r.bars) r.health = S(GetSoldierHealthString(&s));
					}
					else
					{
						if (s.bLevel != 0) r.action = S(gzLateLocalizedString[STR_LATE_15]);
						r.health = S(GetSoldierHealthString(&s));
					}
					r.hpw = std::clamp(int(s.bLife), 0, 100);
					r.enw = std::clamp(int(s.bBreath), 0, 100);
				}
				overlays.push_back(r);
			}
		}

		SlotRow MakeSlot(SOLDIERTYPE const& s, int const pos, bool const big, char const* ghost, std::string const& label)
		{
			SlotRow r;
			r.idx = pos;
			r.big = big;
			r.ghost = ghost;
			r.label = label;
			OBJECTTYPE const& o = s.inv[pos];
			if (o.usItem != NOTHING)
			{
				r.empty = false;
				// long guns in a big slot (136 x 64 dp) and the rest in a small one (64 x 64 dp): never clipped, never stretched
				Pic const p = FitPic("nitem-" + std::to_string(o.usItem), 2, big ? 128.f : 56.f, 48.f);
				r.src = p.src; r.w = p.w; r.h = p.h;
				ItemModel const* const it = GCM->getItem(o.usItem);
				r.title = S(it->getName());
				if (it->isGun()) r.count = std::to_string(o.ubGunShotsLeft);
				else if (o.ubNumberOfObjects > 1) r.count = std::to_string(o.ubNumberOfObjects);
				else if (it->getItemClass() == IC_MONEY) r.count = S(SPrintMoney(o.uiMoneyAmount));
				r.cond = std::clamp(int(o.bStatus[0]), 0, 100);
				r.worn = r.cond < 60;
				r.att = false;
				for (UINT16 const a : o.usAttachItem)
				{
					if (a == NOTHING) continue;
					r.att = true;
					++r.natts;
					r.atts += "<div class=\"ln\">" + Escape(S(GCM->getItem(a)->getName())) + "</div>";
				}
				bool const metric = gGameSettings.fOptions[TOPTION_USE_METRIC_SYSTEM];
				r.wt = ST::format("{.1f} {}", Weight(o) / (metric ? 1000.0 : 453.59237), GetWeightUnitString()).to_std_string();
				if (it->isGun())
				{
					r.ammo = ST::format("{} / {}", o.ubGunShotsLeft, GCM->getWeapon(o.usItem)->ubMagSize).to_std_string();
					if (o.usGunAmmoItem != NOTHING) r.ammo += " \xC2\xB7 " + S(GCM->getItem(o.usGunAmmoItem)->getShortName());
				}
			}
			// something is held: which pockets take it (the core's verdict, drawn as an outline)
			if (InventoryHand().item != NOTHING) r.drop = InventorySlotDrop(const_cast<SOLDIERTYPE*>(&s), pos).ok ? "ok" : "no";
			return r;
		}

		void ReadDetail()
		{
			detail = gsCurInterfacePanel == SM_PANEL && gpSMCurrentMerc;
			dMute = false;
			body.clear(); hands.clear(); lbe.clear(); bigPockets.clear(); smallPockets.clear(); beltPockets.clear(); packPockets.clear(); attrs.clear();
			if (!detail) return;
			SOLDIERTYPE const& s = *gpSMCurrentMerc;
			dName = S(s.name);
			// the paper doll's build, as the original's three inventory figures: big and stocky men share the large one
			dDoll = s.ubBodyType == REGFEMALE ? "gen-doll-female"
				: s.ubBodyType == BIGMALE || s.ubBodyType == STOCKYMALE ? "gen-doll-big" : "gen-doll";
			dMute = (s.uiStatusFlags & SOLDIER_MUTE) != 0;
			std::string full = s.ubProfile != NO_PROFILE ? S(GetProfile(s.ubProfile).zName) : dName;
			dFull = full;
			Pic const f = MakePic("sface-" + std::to_string(s.ubProfile != NO_PROFILE ? GetProfile(s.ubProfile).ubFaceIndex : 0), 2);
			dFace = f.src; dFw = f.w; dFh = f.h;
			dVitHp = ST::format("{}/{}", s.bLife, s.bLifeMax).to_std_string();
			dEn = ST::format("{}/{}", s.bBreath, s.bBreathMax).to_std_string();
			dMo = S(GetMoraleString(s));
			dHpw = std::clamp(int(s.bLife), 0, 100);
			dLostw = std::clamp(int(s.bLifeMax) - s.bLife, 0, 100);
			dEnw = std::clamp(int(s.bBreath), 0, 100);
			dMow = std::clamp(int(s.bMorale), 0, 100);
			int const carried = CalculateCarriedWeight(&s), armour = ArmourPercent(&s);
			dWeight = ST::format("{}%", carried).to_std_string();
			dCamo = ST::format("{}%", s.bCamo).to_std_string();
			dArmour = ST::format("{}%", armour).to_std_string();
			dWeightw = std::clamp(carried, 0, 100);
			dHeavy = carried > 100;
			dCamow = std::clamp(int(s.bCamo), 0, 100);
			dArmourw = std::clamp(armour, 0, 100);
			dMoney = S(SPrintMoney(LaptopSaveInfo.iCurrentBalance));
			int keys = 0;
			if (s.pKeyRing) for (int k = 0; k < NUM_KEYS; ++k) if (s.pKeyRing[k].ubNumber > 0) ++keys;
			dKeys = std::to_string(keys);
			INT8 const values[] = { s.bAgility, s.bDexterity, s.bStrength, s.bLeadership, s.bWisdom,
				s.bExpLevel, s.bMarksmanship, s.bExplosive, s.bMechanical, s.bMedical };
			for (int i = 0; i < 10; ++i)
			{
				// physical (agility, dexterity, strength), mental (leadership, wisdom, level), skills; level runs 1-10
				int const scale = i == 5 ? 10 : 1;
				attrs.push_back({ S(pShortAttributeStrings[i]), std::to_string(values[i]), std::clamp(values[i] * scale, 0, 100), i == 3 || i == 6 });
			}
			body.push_back(MakeSlot(s, HEAD1POS, false, "face-gear", Str("tac.slot.face1")));
			body.push_back(MakeSlot(s, HEAD2POS, false, "face-gear", Str("tac.slot.face2")));
			body.push_back(MakeSlot(s, HELMETPOS, false, "armour", Str("tac.slot.helmet")));
			body.push_back(MakeSlot(s, VESTPOS, false, "armour", Str("tac.slot.vest")));
			body.push_back(MakeSlot(s, LEGPOS, false, "armour", Str("tac.slot.legs")));
			hands.push_back(MakeSlot(s, HANDPOS, true, "gun", Str("tac.slot.hand")));
			hands.push_back(MakeSlot(s, SECONDHANDPOS, true, "gun", Str("tac.slot.offhand")));
			lbe.push_back(MakeSlot(s, LBE_VESTPOS, false, "inventory", Str("tac.slot.lbe_vest")));
			lbe.push_back(MakeSlot(s, LBE_BELTPOS, false, "inventory", Str("tac.slot.lbe_belt")));
			lbe.push_back(MakeSlot(s, LBE_PACKPOS, false, "inventory", Str("tac.slot.lbe_pack")));
			for (int p = POCK1POS; p <= POCK4POS; ++p) bigPockets.push_back(MakeSlot(s, p, false, "inventory", ""));
			for (int p = POCK5POS; p <= POCK12POS; ++p) smallPockets.push_back(MakeSlot(s, p, false, "inventory", ""));
			// the panel shows the pockets by the LBE item that carries them: vest POCK1-4, belt POCK5-8, pack POCK9-12
			beltPockets.assign(smallPockets.begin(), smallPockets.begin() + LBE_WINDOW_SIZE);
			packPockets.assign(smallPockets.begin() + LBE_WINDOW_SIZE, smallPockets.end());
		}

		void ReadDesc()
		{
			bool const was = desc;
			desc = InItemDescriptionBox();
			if (desc && !was) xTab = "general";
			xStats.clear();
			xAtts.clear();
			if (!desc) return;
			NativeItemDescInfo const d = NativeItemDescData();
			descMoney = d.money; descGun = d.gun; descWeapon = d.weapon; descProsCons = d.prosCons; descHatched = d.attachmentsHatched;
			xName = S(d.name); xType = S(d.type); xText = S(d.desc);
			xPros = S(d.pros); xCons = S(d.cons);
			xWeight = S(d.weight) + " " + S(d.weightUnit);
			xStatusLabel = S(d.statusLabel);
			xCond = d.status;
			xStatus = d.statusText.empty() ? ST::format("{}%", d.status).to_std_string() : S(d.statusText);
			xKey = d.keySector.empty() ? "" : S(sKeyDescriptionStrings[0]) + " " + S(d.keySector) + " · " + S(sKeyDescriptionStrings[1]) + " " + S(d.keyDate);
			// the big picture, integer scale, at most 520 x 120 dp
			if (gpItemDescObject)
			{
				Pic const p = FitPic("nitembig-" + std::to_string(gpItemDescObject->usItem), 2, 520, 120);
				xPic = p.src; xPw = p.w; xPh = p.h;
			}
			if (d.weapon)
			{
				// bar scales: damage up to 60, range up to 70, AP costs out of 25 (shorter is better)
				auto pct = [](int v, int max) { return std::clamp(v * 100 / max, 0, 100); };
				if (d.damage >= 0) xStats.push_back({ S(gWeaponStatsDesc[4]), std::to_string(d.damage), "target", pct(d.damage, 60) });
				if (d.range >= 0)  xStats.push_back({ S(gWeaponStatsDesc[3]), std::to_string(d.range), "range", pct(d.range, 70) });
				xStats.push_back({ S(gWeaponStatsDesc[5]), std::to_string(d.aps), "action-points", pct(d.aps, 25), true });
				if (d.burstAps >= 0) xStats.push_back({ ST::format("{} ({})", Str("tac.burst"), d.burstShots).to_std_string(), std::to_string(d.burstAps), "burst", pct(d.burstAps, 25), true });
			}
			xStats.push_back({ S(st_format_printf(gWeaponStatsDesc[0], d.weightUnit)), S(d.weight), "weight",
				std::clamp(int(std::atof(S(d.weight).c_str()) * 10), 0, 100), true });
			if (d.gun)
			{
				xAmmo = ST::format("{} / {}", d.shotsLeft, d.magSize).to_std_string();
				xAmmoType = d.ammoItem != NOTHING ? S(GCM->getItem(d.ammoItem)->getShortName()) : "";
			}
			// Typed slots: one row per slot the platform offers, labelled by
			// role ("optic", "muzzle", ...). Merge-style attachments on items
			// without a platform still show their positions.
			int slotCount = d.attachSlots;
			for (int i = 0; i < 4; ++i) if (d.attachments[i] != NOTHING && i >= slotCount) slotCount = i + 1;
			for (int i = 0; i < slotCount; ++i)
			{
				SlotRow r;
				r.idx = i;
				r.ghost = "add";
				r.label = d.attachRole[i] != nullptr && d.attachRole[i][0] != '\0' ? Str("tac.slotrole." + std::string(d.attachRole[i])) : "";
				if (d.attachments[i] != NOTHING)
				{
					r.empty = false;
					Pic const p = FitPic("nitem-" + std::to_string(d.attachments[i]), 2, 56, 48);
					r.src = p.src; r.w = p.w; r.h = p.h;
					r.title = S(GCM->getItem(d.attachments[i])->getName());
					r.cond = std::clamp(d.attachmentStatus[i], 0, 100);
				}
				xAtts.push_back(r);
			}
			if (d.money)
			{
				InventoryMoneySplit const m = InventoryMoneyState();
				mTotal = S(SPrintMoney(m.total));
				mRemaining = S(SPrintMoney(m.remaining));
				mRemoving = S(SPrintMoney(m.removing));
				m1000 = m.total >= 1000; m100 = m.total >= 100; m10 = m.total >= 10;
			}
		}

		/** The question the inventory core is waiting on, and the reason the last click was refused. */
		void ReadAsk()
		{
			Equipment::Question const& q = TacticalInventory().Pending();
			ask = q.kind != Equipment::QuestionKind::None;
			askText = ask ? Str(q.kind == Equipment::QuestionKind::Merge ? "tac.ask.merge" : "tac.ask.permanent") : std::string();
			InventoryOutcome const& o = LastInventoryOutcome();
			// a refusal is shown for a moment, then goes
			if (o.seq != hintSeq) { hintSeq = o.seq; hintAt = GetJA2Clock(); }
			hint = !o.ok && o.action == "refused" && GetJA2Clock() - hintAt < 2500 ? o.why : std::string();
		}

		/** What a disabled row says: a PopupModels::WhyKey code, in words. */
		std::string WhyText(std::string const& code)
		{
			return code.empty() ? std::string() : Str("tac.why." + code);
		}

		/** The action menu and the door menu, from PopupModels::Menu (Interface.cc), and the pick-up list. */
		void ReadMenus()
		{
			menu.clear();
			pick.clear();
			menuOpen = false;
			pickOpen = false;

			MenuPopup const m = CurrentMenuPopup();
			if (m.open)
			{
				menuOpen = true;
				bool const door = m.door;
				bool const showAp = (gTacticalStatus.uiFlags & INCOMBAT) != 0 && m.apLeft >= 0;
				std::string const ap = showAp ? ST::format("{} {}", m.apLeft, Str("tac.ap")).to_std_string() : std::string();
				menuTitle = door ? Str("tac.menu.door") : S(m.who);
				menuSub = door ? (showAp ? S(m.who) + " \xC2\xB7 " + ap : std::string()) : ap;
				int lastGroup = -1, seq = 0;
				for (PopupRow const& it : m.rows)
				{
					if (it.group != lastGroup)
					{
						if (lastGroup != -1) // a separator between the groups (the approved wireframes)
						{
							MenuItemRow sep;
							sep.kind = "sep";
							sep.eid = "tac.menu.sep[" + std::to_string(seq++) + "]";
							menu.push_back(sep);
						}
						if (!door && it.group <= 1) // the action menu names its groups
						{
							MenuItemRow head;
							head.kind = "head";
							head.label = Str(it.group == 0 ? "tac.menu.move" : "tac.menu.act");
							head.eid = "tac.menu.head[" + std::to_string(seq++) + "]";
							menu.push_back(head);
						}
						lastGroup = it.group;
					}
					MenuItemRow r;
					r.kind = "item";
					r.id = it.cmd;
					r.eid = "tac.menu.item[" + std::to_string(it.cmd) + "]";
					r.label = S(it.label);
					r.kbd = S(it.kbd);
					r.icon = it.icon;
					r.title = S(it.title);
					r.why = it.enabled ? "" : WhyText(it.why);
					r.ap = it.ap;
					r.disabled = !it.enabled;
					menu.push_back(r);
				}
				// the action menu opens by the merc, the door menu by the door (the approved wireframes)
				Rml::Vector2f const at = CanvasToOutput(float(m.x), float(m.y));
				// the action menu opens under the pointer, the door menu by the merc
				float x = at.x + std::round((door ? 36.f : -20.f) * DpScale());
				float y = at.y - std::round(20.f * DpScale());
				int h = 56;
				for (MenuItemRow const& r : menu) h += r.kind == "item" ? (r.why.empty() ? 40 : 60) : 20;
				ClampPopup(x, y, 252, float(h));
				menuX = int(x);
				menuY = int(y);
			}

			PickupPopup const p = CurrentPickupPopup();
			if (p.open)
			{
				pickOpen = true;
				pickTitle = Str("tac.pick.title");
				pickSub = ST::format(Str("tac.pick.sub").c_str(), p.who, p.total).to_std_string();
				pickOk = ST::format(Str("tac.pick.take").c_str(), p.selected).to_std_string();
				pickPage = p.page;
				pickPages = p.pages;
				pickCanUp = p.canUp;
				pickCanDown = p.canDown;
				pickAll = p.all;
				pickEnabled = p.canTake;
				for (PickupRowView const& it : p.rows)
				{
					PickRow r;
					r.slot = it.row;
					r.eid = "tac.pick.item[" + std::to_string(it.row) + "]";
					r.empty = it.empty;
					r.sel = it.sel;
					r.att = it.att;
					r.cond = it.cond;
					r.name = S(it.name);
					r.count = S(it.count);
					r.title = S(it.title);
					if (!it.empty)
					{
						Pic const pic = FitPic("nitem-" + std::to_string(it.item), 2, 88, 40);
						r.src = pic.src; r.w = pic.w; r.h = pic.h;
					}
					pick.push_back(r);
				}
				Rml::Vector2f const at = CanvasToOutput(float(p.x), float(p.y));
				float x = at.x;
				x += std::round(36.f * DpScale());
				float y = at.y - std::round(20.f * DpScale());
				ClampPopup(x, y, 360, float(120 + 56 * int(p.rows.size())));
				pickX = int(x);
				pickY = int(y);
			}
		}

		/** The label of a pocket or worn slot, for "where the stack came from". */
		static std::string SlotName(int const slot)
		{
			switch (slot)
			{
				case HEAD1POS:       return Str("tac.slot.face1");
				case HEAD2POS:       return Str("tac.slot.face2");
				case HELMETPOS:      return Str("tac.slot.helmet");
				case VESTPOS:        return Str("tac.slot.vest");
				case LEGPOS:         return Str("tac.slot.legs");
				case HANDPOS:        return Str("tac.slot.hand");
				case SECONDHANDPOS:  return Str("tac.slot.offhand");
				case LBE_VESTPOS:    return Str("tac.slot.lbe_vest");
				case LBE_BELTPOS:    return Str("tac.slot.lbe_belt");
				case LBE_PACKPOS:    return Str("tac.slot.lbe_pack");
			}
			if (slot >= POCK1POS && slot <= POCK12POS) return ST::format(Str("tac.pocket").c_str(), slot - POCK1POS + 1).to_std_string();
			return std::string();
		}

		/** The big face of an NPC or merc (the small one when there is no big one), at an integer scale. */
		static Pic FacePic(int const face, float const base)
		{
			Pic p = MakePic("bface-" + std::to_string(face), base);
			if (p.src.empty()) p = MakePic("sface-" + std::to_string(face), base);
			return p;
		}

		/** Puts the popups that hang on a part of the HUD where that part is now: the stack popup over the pocket it came
		 * from, the key ring over the key button, the talk panel over the bar (never over the speaker). */
		void Place(Rml::ElementDocument* const doc, float const barPx)
		{
			float const dp = std::max(0.01f, DpScale());
			Rml::Vector2i const dim = Context()->GetDimensions();
			talkBottom = int(std::round(barPx + 12.f * dp));

			auto anchor = [&](std::string const& id, float& cx, float& top, float& bottom) {
				cx = dim.x * 0.5f;
				top = dim.y * 0.5f;
				bottom = top;
				if (Rml::Element* const e = doc ? doc->GetElementById(id) : nullptr)
				{
					Rml::Vector2f const o = e->GetAbsoluteOffset(Rml::BoxArea::Border);
					Rml::Vector2f const sz = e->GetBox().GetSize(Rml::BoxArea::Border);
					if (sz.x > 0)
					{
						cx = o.x + sz.x * 0.5f;
						top = o.y;
						bottom = o.y + sz.y;
					}
				}
			};
			auto put = [&](float const widthDp, float const heightDp, std::string const& id, int& outX, int& outY) {
				float cx, top, bottom;
				anchor(id, cx, top, bottom);
				float const h = heightDp * dp;
				float x = cx - widthDp * dp * 0.5f;
				float y = top - h - 8.f * dp; // above it
				if (y < 8.f * dp) y = bottom + 8.f * dp; // else below
				ClampPopup(x, y, widthDp, heightDp);
				outX = int(x);
				outY = int(y);
			};

			if (stkOpen)
			{
				int const rows = std::max(1, (int(stk.size()) + 4) / 5);
				put(452.f, 150.f + 92.f * rows + (stkCount > 1 ? 40.f : 0.f), "tac.inv.slot[" + std::to_string(stkSlot) + "]", stkX, stkY);
			}
			if (keyOpen)
			{
				put(470.f, 120.f + 64.f * float(std::max<size_t>(1, keys.size())), "tac.inv.keys", keyX, keyY);
			}
		}

		/** The stack popup, the key ring, the talk panel, the speaking face and the sector exit menu. */
		void ReadPopups()
		{
			stk.clear();
			keys.clear();
			talk.clear();
			stkOpen = keyOpen = talkOpen = spOpen = exOpen = false;

			StackPopup const sp = CurrentStackPopup();
			if (sp.open)
			{
				stkOpen = true;
				stkName = S(sp.name);
				std::string const where = SlotName(sp.slot);
				stkWhere = ST::format(Str("tac.stack.where").c_str(), sp.count, S(sp.where)).to_std_string();
				if (!where.empty()) stkWhere += " \xC2\xB7 " + where;
				stkCount = sp.count;
				stkTake = sp.take;
				stkSlot = sp.slot;
				stkHolding = sp.holding;
				stkMore = sp.canMore;
				stkLess = sp.canLess;
				stkTakeLabel = ST::format(Str("tac.stack.n").c_str(), sp.take).to_std_string();
				stkHint = Str(sp.holding ? "tac.stack.hint_put" : "tac.stack.hint");
				Pic const pic = FitPic("nitem-" + std::to_string(sp.item), 2, 60, 48);
				for (StackBox const& b : sp.boxes)
				{
					StkRow r;
					r.i = b.index;
					r.eid = "tac.stack.box[" + std::to_string(b.index) + "]";
					r.filled = b.filled;
					r.low = b.low;
					r.text = S(b.text);
					if (b.filled) { r.src = pic.src; r.w = pic.w; r.h = pic.h; }
					stk.push_back(r);
				}
			}

			KeyRingPopup const kp = CurrentKeyRingPopup();
			if (kp.open)
			{
				keyOpen = true;
				keyWho = S(kp.who);
				keyDoor = kp.door;
				keyHolding = kp.holding;
				keyNone = kp.keys.empty();
				keyHint = ST::format(Str(kp.door ? "tac.key.hint_door" : "tac.key.hint").c_str(), kp.who).to_std_string();
				for (KeyView const& k : kp.keys)
				{
					KeyRow r;
					r.slot = k.slot;
					r.count = k.count;
					r.eid = "tac.keyring.row[" + std::to_string(k.slot) + "]";
					r.use_id = "tac.keyring.use[" + std::to_string(k.slot) + "]";
					r.take_id = "tac.keyring.take[" + std::to_string(k.slot) + "]";
					r.name = S(k.name);
					r.found = k.day > 0 ? ST::format(Str("tac.key.found").c_str(), k.sector, k.day).to_std_string() : S(k.sector);
					r.can_use = k.canUse;
					r.fits = k.fits;
					r.why = WhyText(k.why);
					if (k.item)
					{
						Pic const pic = FitPic("nitem-" + std::to_string(k.item), 2, 52, 44);
						r.src = pic.src; r.w = pic.w; r.h = pic.h;
					}
					keys.push_back(r);
				}
			}

			TalkPopup const tp = CurrentTalkPopup();
			if (tp.open)
			{
				talkOpen = true;
				talkName = S(tp.name);
				talkLine = S(tp.line);
				talkPrev = S(tp.previous);
				talkSpeaking = tp.speaking;
				Pic const f = FacePic(tp.face, 2);
				talkFace = f.src; talkFw = f.w; talkFh = f.h;
				for (TalkRowView const& r : tp.rows)
				{
					TalkRowV v;
					v.approach = r.approach;
					v.eid = "tac.talk." + r.name;
					v.label = S(r.label);
					v.key = r.key;
					v.enabled = r.enabled;
					v.why = WhyText(r.why);
					v.title = r.enabled ? S(r.title) : v.why;
					talk.push_back(v);
				}
			}

			SpeechView const sv = CurrentSpeech();
			// the talk panel shows its own NPC's lines
			if (sv.shown && !talkOpen)
			{
				spOpen = true;
				spWho = S(sv.who);
				spLine = S(sv.line);
				Pic const f = MakePic("sface-" + std::to_string(sv.face), 2);
				spFace = f.src; spFw = f.w; spFh = f.h;
				spBubble = false;
				if (sv.soldier && !sv.line.empty())
				{
					SOLDIERTYPE const* const s = FindSoldierByProfileID(UINT8(sv.profile));
					if (s && s->bInSector && s->bVisible != -1)
					{
						INT16 x, y;
						GetSoldierAboveGuyPositions(s, &x, &y, FALSE);
						Rml::Vector2f const at = CanvasToOutput(float(x + 40), float(y));
						float px = at.x - std::round(150.f * DpScale());
						float py = at.y - std::round(96.f * DpScale());
						ClampPopup(px, py, 300, 80);
						spX = int(px);
						spY = int(py);
						spBubble = true;
					}
				}
				// no words and no merc on screen: the face card still says who is speaking
			}

			ExitPopup const ep = CurrentExitPopup();
			if (ep.open)
			{
				exOpen = true;
				exDir = S(ep.direction);
				exDirLabel = Str("tac.exit.dir." + exDir);
				exTitle = ep.to.empty() ? ST::format(Str("tac.exit.leave").c_str(), ep.from).to_std_string()
					: ST::format(Str("tac.exit.leave_to").c_str(), ep.from, ep.to).to_std_string();
				exSingleLabel = S(ep.selectedLabel) + ": " + S(ep.selected);
				exAllLabel = S(ep.allLabel);
				exLoadLabel = S(ep.loadLabel);
				exSingleCount = Str("tac.exit.one");
				exAllCount = ep.squadSize == 1 ? Str("tac.exit.one") : ST::format(Str("tac.exit.many").c_str(), ep.squadSize).to_std_string();
				exSingleSel = ep.singleSel; exAllSel = ep.allSel; exLoadSel = ep.loadSel;
				exSingleOff = ep.singleOff; exAllOff = ep.allOff; exLoadOff = ep.loadOff;
				exSingleTip = S(ep.singleTip); exAllTip = S(ep.allTip); exLoadTip = S(ep.loadTip);
				exSingleWhy = S(ep.singleTip); exAllWhy = S(ep.allTip); exLoadWhy = S(ep.loadTip);
				if (exSingleWhy.empty()) exSingleWhy = WhyText(ep.singleWhy);
				if (exAllWhy.empty()) exAllWhy = WhyText(ep.allWhy);
				if (exLoadWhy.empty()) exLoadWhy = WhyText(ep.loadWhy);
				exTrip = ep.minutes > 0 ? ST::format(Str(ep.shortTrip ? "tac.exit.trip_short" : "tac.exit.trip").c_str(), ep.minutes).to_std_string()
					: Str("tac.exit.trip_none");
				exCanGo = ep.canGo;
			}
		}
	};

	/** The id of the nearest ancestor named "<prefix><n>]" (tac.inv.slot[3], tac.squad[1]): n, or -1. */
	int IndexOf(Rml::Element* e, std::string const& prefix)
	{
		for (; e; e = e->GetParentNode())
		{
			std::string const& id = e->GetId();
			if (id.size() > prefix.size() && id.compare(0, prefix.size(), prefix) == 0)
				return std::atoi(id.c_str() + prefix.size());
		}
		return -1;
	}

	/** The left button going down and up on the HUD: a press on a pocket may become a drag (see UpdateDrag). */
	class DragListener final : public Rml::EventListener
	{
	public:
		void ProcessEvent(Rml::Event& ev) override
		{
			if (ev.GetParameter<int>("button", 0) != 0) return;
			if (ev.GetType() == "mousedown")
			{
				g_swallowClick = false;
				g_swallowUp = false;
				Rml::Element* const target = ev.GetTargetElement();
				g_downCard = IndexOf(target, "tac.squad[");
				int const slot = IndexOf(target, "tac.inv.slot[");
				// only a full pocket of the merc the panel shows can be dragged, and only with nothing in the hand
				if (slot >= 0 && slot < NUM_INV_SLOTS && gpSMCurrentMerc && gpSMCurrentMerc->inv[slot].usItem != NOTHING &&
					InventoryHand().item == NOTHING)
					g_gesture.Press(ev.GetParameter<float>("mouse_x", 0.f), ev.GetParameter<float>("mouse_y", 0.f), slot);
				else
					g_gesture.Cancel();
			}
			else
			{
				g_gesture.Release();
			}
		}
	};
	DragListener g_dragListener;

	/** Something of the HUD that takes clicks lies within @a dp of @a p: a release there is "on the HUD", not on the
	 * world (a hand that lets go a finger's width outside a panel does not throw the item). */
	bool NearHud(Rml::Vector2f const p, float const dp)
	{
		float const r = 16.f * dp;
		for (Rml::Vector2f const d : { Rml::Vector2f(0, 0), Rml::Vector2f(r, 0), Rml::Vector2f(-r, 0), Rml::Vector2f(0, r),
			Rml::Vector2f(0, -r), Rml::Vector2f(r, r), Rml::Vector2f(-r, r), Rml::Vector2f(r, -r), Rml::Vector2f(-r, -r) })
		{
			Rml::Element* e = Context()->GetElementAtPoint(p + d);
			for (; e; e = e->GetParentNode())
				if (e->IsClassSet("hit")) return true;
		}
		return false;
	}

	/** A press on a pocket that moved a few dp is a drag: the item is in the hand, riding on the pointer. Where it is
	 * let go decides the rest: on a pocket or card the HUD's own mouse-up (slot / card_drop), on the world the legacy
	 * click handler (drop, throw, give) that has always acted on a held item. A drag never leaves the item stuck: Esc
	 * or the right button while dragging, a release on nothing or a refusal, and a release at the edge of a panel all
	 * send it back where it came from. */
	void UpdateDrag()
	{
		bool const down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
		bool const right = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
		bool const rightEdge = right && !g_rightWasDown;
		g_rightWasDown = right;
		if (g_gesture.Pressed())
		{
			if (!down)
			{
				// let go where the HUD does not get the button (the world): the release is handled below
				g_gesture.Cancel();
			}
			else
			{
				Rml::Vector2f const m = MousePosition();
				if (g_gesture.Move(m.x, m.y, Equipment::DRAG_THRESHOLD_DP * std::max(0.01f, DpScale())))
				{
					InventorySlotClick(gpSMCurrentMerc, g_gesture.Slot(), false, false);
					g_dragLifted = InventoryHand().item != NOTHING;
				}
			}
		}
		// a click-to-pick item: the right button anywhere but on a pocket puts it back
		if (!g_dragLifted && rightEdge && InventoryHand().item != NOTHING && !TacticalInventory().Asking() &&
			IndexOf(Context()->GetHoverElement(), "tac.desc") < 0 && !InItemDescriptionBox())
		{
			// a pocket with something in it is a swap / describe; an empty one, or anywhere else, puts it back
			int const over = IndexOf(Context()->GetHoverElement(), "tac.inv.slot[");
			if (over < 0 || !gpSMCurrentMerc || over >= NUM_INV_SLOTS || gpSMCurrentMerc->inv[over].usItem == NOTHING)
				CancelItemPointer();
		}
		// Esc does the same for an item held by a click
		bool const esc = _KeyDown(SDLK_ESCAPE);
		bool const escEdge = esc && !g_escWasDown;
		g_escWasDown = esc;
		if (!g_dragLifted && escEdge && InventoryHand().item != NOTHING && !TacticalInventory().Asking() && !InItemDescriptionBox())
			CancelItemPointer();
		if (!g_dragLifted) return;
		if (InventoryHand().item == NOTHING)
		{
			g_dragLifted = false; // put down by the HUD's own mouse-up
			return;
		}
		if (down && (rightEdge || _KeyDown(SDLK_ESCAPE)))
		{
			// changed his mind: back in the pocket, at no cost; the release that follows does nothing
			CancelItemPointer();
			g_dragLifted = false;
			g_swallowUp = true;
			return;
		}
		if (down) { g_releaseSeen = false; return; }
		// the HUD's own mouse-up is processed with the frame after the button went up: look at the result then
		if (!g_releaseSeen) { g_releaseSeen = true; return; }
		g_releaseSeen = false;
		g_dragLifted = false;
		// released: over a pocket or a card the HUD's mouse-up has already put it down (or refused). What is left in the
		// hand goes to the world only when it was let go clearly outside the HUD.
		if (TacticalInventory().Asking() || InItemDescriptionBox()) return;
		if (TacticalHudWantsMouse() || NearHud(MousePosition(), std::max(0.01f, DpScale())))
		{
			CancelItemPointer();
			return;
		}
		if (!DropHeldAtCursor()) CancelItemPointer();
	}

	/** The first "{}" of @a fmt replaced. */
	std::string Sub(std::string fmt, std::string const& a)
	{
		size_t const i = fmt.find("{}");
		if (i != std::string::npos) fmt.replace(i, 2, a);
		return fmt;
	}

	char const* WhyKey(Equipment::InvWhy const why)
	{
		using W = Equipment::InvWhy;
		switch (why)
		{
			case W::OutOfReach:    return "out_of_reach";
			case W::NoAP:          return "no_ap";
			case W::NoAPTarget:    return "no_ap_target";
			case W::Unconscious:   return "unconscious";
			case W::NailsVest:     return "nails_vest";
			case W::NotAttachable: return "not_attachable";
			case W::DoesNotFit:    return "does_not_fit";
			case W::HandFull:      return "hand_full";
			default:               return "cannot";
		}
	}

	/** With something in the hand, the chip over a pocket or a squad card says what letting go would do. */
	void UpdateDropChip(TacticalViewModel const& vm)
	{
		HudChip chip;
		Equipment::HeldStack const& hand = InventoryHand();
		if (hand.item != NOTHING)
		{
			Rml::Element* const hover = Context()->GetHoverElement();
			int const card = IndexOf(hover, "tac.squad[");
			int const slot = card < 0 ? IndexOf(hover, "tac.inv.slot[") : -1;
			if (card >= 0)
			{
				SOLDIERTYPE* const s = card < NUM_TEAM_SLOTS ? GetPlayerFromInterfaceTeamSlot(UINT8(card)) : nullptr;
				if (s)
				{
					Equipment::DropVerdict const v = PlanDropOnCard(s);
					std::string const name = S(s->name);
					chip.shown = true;
					chip.tone = v.Ok() ? "ok" : "no";
					bool const self = gpItemPointerSoldier == s;
					if (v.action == Equipment::DropAction::Give)
					{
						chip.head = Sub(Str("tac.drag.give"), name);
						chip.lines.push_back({ Str("tac.cur.k_range"), (v.tiles == 1 ? Str("tac.drag.tile") : Sub(Str("tac.cur.range_tiles"), std::to_string(v.tiles))) });
						if (vm.combat) chip.lines.push_back({ Str("tac.cur.k_ap"), Sub(Str("tac.drag.ap_each"), std::to_string(v.apGiver)) });
					}
					else if (v.action == Equipment::DropAction::DropAtFeet)
					{
						chip.head = Str("tac.drag.feet");
						if (vm.combat && v.apGiver > 0) chip.lines.push_back({ Str("tac.cur.k_ap"), std::to_string(v.apGiver) });
					}
					else
					{
						chip.head = self ? Str("tac.drag.cannot_feet") : Sub(Str("tac.drag.cannot_give"), name);
						chip.why = Str(std::string("tac.drag.why.") + WhyKey(v.why));
						if (v.why == Equipment::InvWhy::OutOfReach && !self)
							chip.lines.push_back({ Str("tac.cur.k_range"), (v.tiles == 1 ? Str("tac.drag.tile") : Sub(Str("tac.cur.range_tiles"), std::to_string(v.tiles))) });
					}
				}
			}
			else if (slot >= 0 && gpSMCurrentMerc)
			{
				SlotDrop const d = InventorySlotDrop(gpSMCurrentMerc, slot);
				std::string const item = S(GCM->getItem(hand.item)->getName());
				std::string occupant;
				for (auto const* rows : { &vm.body, &vm.hands, &vm.lbe, &vm.bigPockets, &vm.smallPockets, &vm.beltPockets, &vm.packPockets })
					for (SlotRow const& r : *rows)
						if (r.idx == slot) occupant = r.title;
				chip.shown = true;
				chip.tone = d.ok ? "ok" : "no";
				if (d.ok)
				{
					if (d.kind == Equipment::PlaceKind::Attach) chip.head = Sub(Str("tac.drag.attach"), occupant);
					else if (d.kind == Equipment::PlaceKind::AskMerge) chip.head = Sub(Str("tac.drag.merge"), occupant);
					else chip.head = Sub(Str("tac.drag.put"), item);
					if (vm.combat && d.apFrom > 0) chip.lines.push_back({ Str("tac.cur.k_ap"), std::to_string(d.apFrom) });
				}
				else
				{
					chip.head = Sub(Str("tac.drag.cannot_put"), item);
					chip.why = Str(std::string("tac.drag.why.") + WhyKey(d.why));
				}
			}
			else if (TacticalHudWantsMouse() || NearHud(MousePosition(), std::max(0.01f, DpScale())))
			{
				// over a part of the HUD that takes nothing: say how to get out of it
				chip.shown = true;
				chip.tone = "warn";
				chip.head = Sub(Str("tac.drag.holding"), S(GCM->getItem(hand.item)->getName()));
				chip.lines.push_back({ Str("tac.drag.k_back"), Str("tac.drag.back") });
			}
		}
		SetHudChip(std::move(chip));
	}

	struct Hud
	{
		std::unique_ptr<TacticalViewModel> vm;
		std::unique_ptr<Binding> binding;
		Rml::ElementDocument* doc = nullptr;
		bool active = false;
		std::string signature;
		size_t logRows = 0;
	};
	Hud g_hud;

	/** The bar reaches up to the top of the legacy panel it hides: that panel is on the canvas, which the window scales and
	 * letterboxes, so its top is a canvas point in output pixels (not the panel height times the UI scale). */
	float BarHeightPx()
	{
		int const panelH = gsCurInterfacePanel == SM_PANEL ? INV_INTERFACE_HEIGHT : TEAMPANEL_HEIGHT;
		float const panelTop = CanvasToOutput(0, float(SCREEN_HEIGHT - panelH)).y;
		float const legacy = std::ceil(float(Context()->GetDimensions().y) - panelTop);
		return std::max(legacy, std::round(156 * DpScale()));
	}

	std::string Signature(TacticalViewModel& vm)
	{
		return vm.Snapshot().ToJson();
	}

	bool Wanted()
	{
		if (guiCurrentScreen != GAME_SCREEN) return false;
		if (ResolveMode("tactical") != UiMode::Native) return false;
		return true;
	}

	// The document and its data model stay loaded once made: hiding is enough, and removing a data model under a
	// document that RmlUi closes only at its next update would break the document's bindings.
	void Close()
	{
		SetCursorItem({}, 0, 0);
		SetCursorShape({}, "ok", false);
		if (g_hud.doc && g_hud.doc->IsVisible()) { g_hud.doc->Hide(); Invalidate(); }
		if (g_hud.active)
		{
			g_hud.active = false;
			fInterfacePanelDirty = DIRTYLEVEL2;
			SetRenderFlags(RENDER_FLAG_FULL);
		}
	}
}

bool TacticalHudActive() { return g_hud.active; }

void TacticalHudToggleLog()
{
	if (g_hud.vm) g_hud.vm->Invoke("log");
}

void TacticalHudUpdate()
{
	if (!Wanted() || !Start())
	{
		Close();
		return;
	}
	if (!g_hud.doc)
	{
		RegisterTacticalMockImages();
		g_hud.vm = std::make_unique<TacticalViewModel>();
		g_hud.vm->Update(true);
		g_hud.binding = std::make_unique<Binding>(Context(), *g_hud.vm);
		try
		{
			g_hud.doc = LoadDocument("screens/tactical.rml");
		}
		catch (std::exception const& e)
		{
			SLOGE("native tactical HUD: {}", e.what());
			g_hud.binding.reset();
			g_hud.vm.reset();
			return;
		}
		g_hud.doc->AddEventListener("mousedown", &g_dragListener, true);
		g_hud.doc->AddEventListener("mouseup", &g_dragListener, true);
	}
	if (!g_hud.active)
	{
		g_hud.doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
		g_hud.active = true;
		g_hud.signature.clear();
		SetRenderFlags(RENDER_FLAG_FULL);
	}
	g_hud.vm->Refresh();
	g_hud.vm->Place(g_hud.doc, BarHeightPx());
	UpdateDrag();
	// an item held by the mouse rides on the native pointer, at the integer scale of the inventory slots
	if (InventoryHand().item != NOTHING)
	{
		Pic const p = FitPic("nitem-" + std::to_string(InventoryHand().item), 2, 128, 48);
		SetCursorItem(p.src, p.w, p.h);
	}
	else
	{
		SetCursorItem({}, 0, 0);
	}
	std::string sig = Signature(*g_hud.vm);
	if (sig != g_hud.signature)
	{
		g_hud.signature = std::move(sig);
		g_hud.vm->Changed();
		Invalidate(2);
		// the log shows the newest message, at the bottom
		if (g_hud.vm->logOpen && g_hud.vm->log.size() != g_hud.logRows)
		{
			g_hud.logRows = g_hud.vm->log.size();
			g_hud.doc->UpdateDocument();
			if (Rml::Element* list = g_hud.doc->GetElementById("tac.log.list")) list->SetScrollTop(list->GetScrollHeight());
		}
	}
	// the bar reaches up to the top of the legacy panel it hides: that panel is on the canvas, which the window
	// scales and letterboxes, so its top is a canvas point in output pixels (not the panel height times the UI scale)
	if (Rml::Element* bar = g_hud.doc->GetElementById("tac.bar"))
	{
		float const want = BarHeightPx();
		bar->SetProperty(Rml::PropertyId::Height, Rml::Property(want, Rml::Unit::PX));
		// only as many cards as fit whole (a card is at least 236 dp wide); the rest wait for a wider view
		if (Rml::Element* cards = g_hud.doc->GetElementById("tac.squad"))
		{
			int const fit = std::max(1, int(std::floor(cards->GetClientWidth() / (236.f * DpScale()))));
			for (int i = 0; i < cards->GetNumChildren(); ++i)
				cards->GetChild(i)->SetProperty(Rml::PropertyId::Display, Rml::Property(i < fit ? Rml::Style::Display::Flex : Rml::Style::Display::None));
		}
		// detail panel and description side by side need 1580 dp; on a narrower view the description takes its place
		bool const narrow = float(Context()->GetDimensions().x) < 1580.f * DpScale();
		if (Rml::Element* d = g_hud.doc->GetElementById("tac.detail"))
		{
			bool const hide = narrow && g_hud.vm && g_hud.vm->desc;
			d->SetProperty(Rml::PropertyId::Visibility, Rml::Property(hide ? Rml::Style::Visibility::Hidden : Rml::Style::Visibility::Visible));
		}
		if (Rml::Element* d = g_hud.doc->GetElementById("tac.desc"))
			d->SetProperty(Rml::PropertyId::Left, Rml::Property(narrow ? std::round(12 * DpScale()) : std::round(968 * DpScale()), Rml::Unit::PX));
		// the panels above the bar sit on it, whatever its height
		for (char const* id : { "tac.detail", "tac.desc" })
		{
			if (Rml::Element* e = g_hud.doc->GetElementById(id))
				e->SetProperty(Rml::PropertyId::Bottom, Rml::Property(want + std::round(8 * DpScale()), Rml::Unit::PX));
		}
	}
	// what letting go of a held item would do, over a pocket or a card
	UpdateDropChip(*g_hud.vm);
	// the marker, path and chip over the world, and the pointer's shape
	TacticalCursorUpdate(g_hud.doc);
	TacticalOverlaysUpdate(g_hud.doc);
	// the sector card's picture of the sector and where the view is
	TacticalMinimapUpdate(g_hud.doc);
}

bool TacticalHudWantsMouse()
{
	if (!g_hud.active || !g_hud.doc) return false;
	Rml::Element* e = Context()->GetHoverElement();
	for (; e; e = e->GetParentNode())
	{
		if (e->IsClassSet("hit")) return true;
	}
	return false;
}

bool TacticalHudOwnsCursor()
{
	// the pointer over the world is native too (shape and tone from the CursorModel, #318), so it does not change
	// look between world and HUD
	return g_hud.active;
}

void TacticalHudShutdown() { Close(); }

}
