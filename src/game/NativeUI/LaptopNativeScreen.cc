// The native laptop screen (docs/ui/laptop.md): the RML document over LaptopViewModel, the keyboard, the message boxes
// and leaving for the map. Also the I.M.P., florist and insurance pages of the view model.
#include "LaptopViewModel.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"

#include "Cursor_Control.h"
#include "Dialogue_Control.h"
#include "Finances.h"
#include "GameLoop.h"
#include "Input.h"
#include "Laptop.h"
#include "LaptopNative.h"
#include "LaptopSave.h"
#include "MessageBoxScreen.h"
#include "Soldier_Profile.h"
#include "Text.h"
#include "Video.h"

#include <string_theory/format>

#include <algorithm>
#include <optional>

namespace NativeUI
{

namespace
{
	std::string S(ST::string const& s) { return s.to_std_string(); }
}

// ---- I.M.P. ------------------------------------------------------------------------------------------------------

void LaptopViewModel::ReadImp()
{
	impCanCreate = LaptopNative::ImpCanCreate();
	impCost = labels["imp_cost"];
	auto const quiz = LaptopNative::ImpQuiz(impFemale);
	if (impAnswers.size() != quiz.size()) impAnswers.assign(quiz.size(), -1);
	if (impAttrValues.size() != 10) { impAttrValues.assign(10, 55); impBonus = LaptopNative::ImpBonusPoints(); }

	static char const* const steps[] = { "begin", "personality", "attributes", "portrait", "voice", "finish" };
	int const stepIndex = int(std::find(std::begin(steps), std::end(steps), impStep) - std::begin(steps));
	impSteps.clear();
	for (int i = 0; i < 6; ++i)
	{
		Row r; r.index = i; r.id = steps[i]; r.title = Str(std::string("laptop.imp_step_") + steps[i]);
		r.on = i == stepIndex; r.alt = i < stepIndex;
		impSteps.push_back(r);
	}

	impText.clear();
	int const textPage = impStep == "home" ? 0 : impStep == "about" ? 1 : impStep == "main" ? 2 : impStep == "begin" ? 3 :
		impStep == "personality" ? 4 : impStep == "attributes" ? 5 : impStep == "portrait" ? 6 : impStep == "voice" ? 7 : 8;
	for (ST::string const& t : LaptopNative::ImpPageText(textPage)) impText.push_back(S(t));

	impQuestion = std::clamp(impQuestion, 0, std::max(0, int(quiz.size()) - 1));
	impAnswerRows.clear();
	impProgress.clear();
	if (!quiz.empty())
	{
		auto const& q = quiz[impQuestion];
		impQuestionText = S(q.text);
		impQuestionNo = ST::format(Str("laptop.imp_question_of").c_str(), impQuestion + 1, quiz.size()).to_std_string();
		for (size_t a = 0; a < q.answers.size(); ++a)
		{
			Row r; r.index = int(a); r.title = S(q.answers[a]); r.id = std::to_string(a + 1); r.on = impAnswers[impQuestion] == int(a);
			impAnswerRows.push_back(r);
		}
		for (size_t i = 0; i < quiz.size(); ++i)
		{
			Row r; r.index = int(i); r.on = int(i) == impQuestion; r.alt = impAnswers[i] >= 0;
			impProgress.push_back(r);
		}
	}

	impAttrs.clear();
	static int const statOrder[] = { 0, 1, 2, 3, 4, 5, 7, 10, 8, 9 }; // str_stat_list: health ... medical
	for (int i = 0; i < 10; ++i)
	{
		Row r; r.index = i; r.title = statLabels[statOrder[i]]; r.n = impAttrValues[i]; r.value = std::to_string(impAttrValues[i]);
		r.m = impAttrValues[i] * 100 / std::max(1, LaptopNative::ImpAttributeMax());
		r.alt = i >= 6;
		impAttrs.push_back(r);
	}

	impPortraits.clear();
	for (auto const& p : LaptopNative::ImpPortraits(impFemale))
	{
		Row r; r.index = p.index; r.icon = "face-" + std::to_string(p.face); r.on = p.index == impPortrait;
		impPortraits.push_back(r);
	}
	impVoices.clear();
	for (auto const& v : LaptopNative::ImpVoices(impFemale))
	{
		Row r; r.index = v.index; r.title = ST::format(pImpButtonText[5].c_str(), v.index + 1).to_std_string(); r.on = v.index == impVoice;
		impVoices.push_back(r);
	}
	impSkills.clear();
	if (LaptopNative::ImpPicksSkillsDirectly())
	{
		auto const names = LaptopNative::ImpSkillNames();
		for (size_t i = 1; i < names.size(); ++i)
		{
			Row r; r.index = int(i); r.title = S(names[i]);
			r.on = std::find(impSkillPick.begin(), impSkillPick.end(), int(i)) != impSkillPick.end();
			impSkills.push_back(r);
		}
	}
	Changed();
}

// ---- florist and insurance --------------------------------------------------------------------------------------

void LaptopViewModel::ReadFlorist()
{
	flowers.clear();
	for (auto const& f : LaptopNative::Flowers())
	{
		Row r; r.index = f.index; r.title = S(f.name); r.meta = S(f.price); r.sub = S(f.description); r.on = f.index == flowerSel;
		flowers.push_back(r);
	}
	floristTowns.clear();
	int i = 0;
	for (ST::string const& t : LaptopNative::FloristTowns())
	{
		Row r; r.index = i; r.title = S(t); r.on = i == flowerTown; ++i;
		floristTowns.push_back(r);
	}
	floristCards.clear();
	i = 0;
	for (ST::string const& c : LaptopNative::FloristCards())
	{
		Row r; r.index = i++; r.title = S(c);
		floristCards.push_back(r);
	}
	LaptopNative::FlowerOrder const o{ flowerSel, flowerTown, flowerNextDay, ST::string(flowerMessage), ST::string(flowerName) };
	flowerCost = S(SPrintMoney(LaptopNative::FlowerOrderCost(o)));
	Changed();
}

void LaptopViewModel::ReadInsurance()
{
	insuranceText.clear();
	int const p = page == "info" ? 1 : page == "comments" ? 2 : page == "contract" ? 3 : 0;
	for (ST::string const& t : LaptopNative::InsuranceText(p)) insuranceText.push_back(S(t));
	insuranceRows.clear();
	for (auto const& r : LaptopNative::InsuranceRows())
	{
		MERCPROFILESTRUCT const& prof = GetProfile(r.profile);
		Row row; row.index = int(insuranceRows.size()); row.id = std::to_string(r.profile); row.title = S(r.name);
		row.icon = "face-" + std::to_string(prof.ubFaceIndex); row.meta = S(SPrintMoney(r.premium));
		row.sub = std::to_string(r.daysLeft); row.on = r.insured; row.alt = r.canInsure;
		insuranceRows.push_back(row);
	}
	Changed();
}

// ---- the screen --------------------------------------------------------------------------------------------------

namespace
{
	struct Pending { std::string what, arg; };
	std::optional<Pending> g_pending;
	bool g_answered = false, g_yes = false;

	void Answer(MessageBoxReturnValue const r)
	{
		g_answered = true;
		g_yes = r == MSG_BOX_RETURN_YES;
	}

	class LaptopScreen final : public Screen
	{
	public:
		void Enter() override
		{
			LaptopNative::Enter();
			RegisterLaptopImages();
			g_pending.reset();
			g_answered = false;
			m_vm.onAction = [this](std::string const& a) { Action(a); };
			m_vm.onAsk = [this](std::string const& what, std::string const& arg) { Ask(what, arg); };
			m_vm.onToast = [](std::string const& text, int kind) { Toast(text, ToastKind(kind)); };
			m_vm.Load();
			m_binding.emplace(Context(), m_vm);
			m_doc = LoadDocument("screens/laptop.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
			SetCompact(m_doc, "laptop", 1500);
			if (int const n = LaptopNative::UnreadMails()) Toast(ST::format(Str("laptop.new_mail").c_str(), n).to_std_string(), ToastKind::Info);
		}

		ScreenID Handle() override
		{
			if (g_answered)
			{
				g_answered = false;
				std::optional<Pending> const p = g_pending;
				g_pending.reset();
				if (p && g_yes) m_vm.Confirm(p->what, p->arg);
			}
			InputAtom e;
			while (DequeueEvent(&e))
			{
				bool const used = ProcessKey(e);
				if (e.usEvent != KEY_UP && e.usEvent != KEY_DOWN && e.usEvent != KEY_REPEAT) continue;
				bool const down = e.usEvent == KEY_DOWN || e.usEvent == KEY_REPEAT;
				if (e.usEvent == KEY_UP && e.usParam == SDLK_ESCAPE) { Escape(); continue; }
				if (used || !down) continue;
				bool const typing = FocusedId().find(".input") != std::string::npos;
				if (typing) continue;
				switch (e.usParam)
				{
					case SDLK_F1: m_vm.OpenApp("email"); break;
					case SDLK_F2: m_vm.OpenApp("web"); break;
					case SDLK_F3: m_vm.OpenApp("finances"); break;
					case SDLK_F4: m_vm.OpenApp("personnel"); break;
					case SDLK_F5: m_vm.OpenApp("history"); break;
					case SDLK_F6: m_vm.OpenApp("files"); break;
					case SDLK_TAB: CycleApp((e.usKeyState & SHIFT_DOWN) || (e.usKeyState & CTRL_DOWN) ? -1 : 1); break;
					case SDLK_UP:   if (m_vm.app == "email") m_vm.MailStep(-1); break;
					case SDLK_DOWN: if (m_vm.app == "email") m_vm.MailStep(1); break;
					case SDLK_DELETE: if (m_vm.app == "email" && m_vm.mailOpen >= 0) Ask("mail_delete", std::to_string(m_vm.mailOpen)); break;
					case SDLK_LEFT: if (e.usKeyState & ALT_DOWN) m_vm.Invoke("back"); break;
					case SDLK_RIGHT: if (e.usKeyState & ALT_DOWN) m_vm.Invoke("forward"); break;
					case 'x': if (e.usKeyState & ALT_DOWN) HandleShortCutExitState(); break;
					default: break;
				}
			}
			SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);
			return m_next;
		}

		void Resized() override { SetCompact(m_doc, "laptop", 1500); }

		void Exit() override
		{
			if (m_doc) CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
			StopAnyCurrentlyTalkingSpeech();
			// the legacy laptop (a smaller video mode, or the next visit) enters afresh; this is also what leaving does
			LaptopNative::Exit();
		}

	private:
		void CycleApp(int const d)
		{
			static char const* const apps[] = { "desktop", "email", "web", "finances", "personnel", "history", "files" };
			int i = 0;
			for (int k = 0; k < 7; ++k) if (m_vm.app == apps[k]) i = k;
			m_vm.OpenApp(apps[(i + d + 7) % 7]);
		}

		void Escape()
		{
			// the topmost thing first: the open mail, the hire panel, the web page, the program, then the laptop
			if (m_vm.app == "email" && m_vm.mailOpen >= 0) { m_vm.Invoke("mail_close"); return; }
			if (m_vm.app == "web" && !m_vm.hireState.empty()) { m_vm.Invoke("aim_hangup"); return; }
			if (m_vm.app != "desktop") { m_vm.OpenApp("desktop"); return; }
			Action("close");
		}

		void Action(std::string const& a)
		{
			if (a == "close" && m_next == LAPTOP_SCREEN) m_next = LaptopNative::Leave();
			else if (a == "rain") DoMessageBox(MSG_BOX_BASIC_STYLE, pErrorStrings, LAPTOP_SCREEN, MSG_BOX_FLAG_OK, nullptr, nullptr);
			else if (a == "help") Toast(Str("laptop.help_text"), ToastKind::Info, Str("laptop.help"));
		}

		void Ask(std::string const& what, std::string const& arg)
		{
			if (g_pending) return;
			ST::string text;
			if (what == "mail_delete") text = Str("laptop.ask_delete_mail");
			else if (what == "merc_pay") text = st_format_printf(MercAccountText[MERC_ACCOUNT_AUTHORIZE_CONFIRMATION], SPrintMoney(INT32(LaptopNative::MercOwed())));
			else if (what == "order")
			{
				for (auto const& d : LaptopNative::Destinations()) if (std::to_string(d.id) == arg) text = st_format_printf(BobbyROrderFormText[BOBBYR_CONFIRM_DEST], d.name);
			}
			else if (what == "imp_create") text = ST::format(Str("laptop.ask_imp").c_str(), SPrintMoney(LaptopNative::ImpCost()));
			else if (what == "flower_send") text = Str("laptop.ask_flowers");
			else text = what;
			g_pending = Pending{ what, arg };
			g_answered = false;
			DoMessageBox(MSG_BOX_BASIC_STYLE, text, LAPTOP_SCREEN, MSG_BOX_FLAG_YESNO, Answer, nullptr);
		}

		LaptopViewModel m_vm;
		std::optional<Binding> m_binding;
		Rml::ElementDocument* m_doc = nullptr;
		ScreenID m_next = LAPTOP_SCREEN;
	};

	bool const g_registered = (RegisterViewModelFactory("laptop", [] {
		auto vm = std::make_unique<LaptopViewModel>();
		vm->Load();
		return std::unique_ptr<ViewModel>(std::move(vm));
	}), true);
}

std::unique_ptr<Screen> CreateLaptopScreen()
{
	return std::make_unique<LaptopScreen>();
}

}
