#include "CharProfile.h"
#include "Directories.h"
#include "Font.h"
#include "IMPVideoObjects.h"
#include "Merc_Hiring.h"
#include "Text.h"
#include "Cursors.h"
#include "Laptop.h"
#include "IMP_Compile_Character.h"
#include "Sound_Control.h"
extern INT32 iCurrentVoices; // IMP_Voices.cc
#include "IMP_Text_System.h"
#include "IMP_Confirm.h"
#include "Items.h"
#include "Finances.h"
#include "Soldier_Profile.h"
#include "Soldier_Profile_Type.h"
#include "Soldier_Control.h"
#include "IMP_Portraits.h"
#include "History.h"
#include "Game_Clock.h"
#include "Game_Event_Hook.h"
#include "LaptopSave.h"
#include "SaveLoadGame.h"
#include "Strategic.h"
#include "Random.h"
#include "Button_System.h"
#include "Font_Control.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "policy/GamePolicy.h"
#include "policy/IMPPolicy.h"

#include <string_theory/string>


static BUTTON_PICS* giIMPConfirmButtonImage[2];
GUIButtonRef giIMPConfirmButton[2];


static void BtnIMPConfirmNo(GUI_BUTTON *btn, UINT32 reason);
static void BtnIMPConfirmYes(GUI_BUTTON *btn, UINT32 reason);


static void CreateConfirmButtons(void);


void EnterIMPConfirm( void )
{
	// create buttons
	CreateConfirmButtons( );
}

void RenderIMPConfirm( void )
{

	// the background
	RenderProfileBackGround( );

	// indent
	RenderAvgMercIndentFrame(90, 40 );

	// highlight answer
	PrintImpText( );
}


static void DestroyConfirmButtons(void);


void ExitIMPConfirm( void )
{
	// destroy buttons
	DestroyConfirmButtons( );
}

void HandleIMPConfirm( void )
{
}


static void MakeButton(UINT idx, const ST::string& text, INT16 y, GUI_CALLBACK click)
{
	BUTTON_PICS* const img = LoadButtonImage(LAPTOPDIR "/button_2.sti", 0, 1);
	giIMPConfirmButtonImage[idx] = img;
	const INT16 text_col   = FONT_WHITE;
	const INT16 shadow_col = DEFAULT_SHADOW;
	GUIButtonRef const btn = CreateIconAndTextButton(img, text, FONT12ARIAL, text_col, shadow_col, text_col, shadow_col, LAPTOP_SCREEN_UL_X + 136, y, MSYS_PRIORITY_HIGH, click);
	giIMPConfirmButton[idx] = btn;
	btn->SetCursor(CURSOR_WWW);
}


static void CreateConfirmButtons(void)
{
	// create buttons for confirm screen
	const INT16 dy = LAPTOP_SCREEN_WEB_UL_Y;
	MakeButton(0, pImpButtonText[16], dy + 254, BtnIMPConfirmYes);
	MakeButton(1, pImpButtonText[17], dy + 314, BtnIMPConfirmNo);
}


static void DestroyConfirmButtons(void)
{
	// destroy buttons for confirm screen

	RemoveButton(giIMPConfirmButton[ 0 ] );
	UnloadButtonImage(giIMPConfirmButtonImage[ 0 ] );


	RemoveButton(giIMPConfirmButton[ 1 ] );
	UnloadButtonImage(giIMPConfirmButtonImage[ 1 ] );
}


static void GiveItemsToPC(UINT8 ubProfileId);


static BOOLEAN AddCharacterToPlayersTeam(void)
{
	MERC_HIRE_STRUCT HireMercStruct{};

	// last minute change to make sure merc with right face has not only the right body, but body specific skills...
	// ie. small mercs have martial arts, but big guys and women don't
	if (!fLoadingCharacterForPreviousImpProfile)
	{
		HandleMercStatsForChangesInFace();
	}

	HireMercStruct.ubProfileID = GetIMPSlotInProgress() ;

	if (!fLoadingCharacterForPreviousImpProfile)
	{
		// give them items
		GiveItemsToPC( 	HireMercStruct.ubProfileID );
	}

	HireMercStruct.bWhatKindOfMerc = MERC_TYPE__PLAYER_CHARACTER;

	HireMercStruct.sSector = g_merc_arrive_sector;
	HireMercStruct.fUseLandingZoneForArrival = TRUE;

	HireMercStruct.fCopyProfileItemsOver = TRUE;

	// indefinite contract length
	HireMercStruct.iTotalContractLength = -1;

	HireMercStruct.ubInsertionCode	= INSERTION_CODE_ARRIVING_GAME;
	HireMercStruct.uiTimeTillMercArrives = GetMercArrivalTimeOfDay( );

	IMPPortrait const& portrait = GetCurrentIMPPortrait();
	SetProfileFaceData(HireMercStruct.ubProfileID, portrait.face, portrait.eyesX, portrait.eyesY, portrait.mouthX, portrait.mouthY);

	//if we succesfully hired the merc
	return HireMerc(HireMercStruct);
}

static void BtnIMPConfirmYes(GUI_BUTTON *btn, UINT32 reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP)
	{
		if (!CanCreateAnotherIMPCharacter())
		{
			// already made as many I.M.P. characters as allowed, leave
			return;
		}

		if (LaptopSaveInfo.iCurrentBalance < COST_OF_PROFILE)
		{
			// not enough
			return;
		}

		// Taken before the slot is claimed, after which the first free slot is
		// the next character's, not this one's.
		ProfileID const profile = GetIMPSlotInProgress();

		// line moved by CJC Nov 28 2002 to AFTER the check for money
		if (!AddCharacterToPlayersTeam()) return; // only if merc hiring failed: no charge, give it another go

		// holds the profile for the rest of the campaign
		MarkIMPCharacterCreated(profile);

		SOLDIERTYPE* const pSoldier = FindSoldierByProfileID(profile);
		if (!pSoldier) return;

		if (fLoadingCharacterForPreviousImpProfile && gamepolicy(imp_load_keep_inventory))
		{
			IMPSavedProfileLoadInventory(gMercProfiles[profile].zNickname, pSoldier);
			// re-add letter, since it just got wiped and almost certainly is not present in the import
			if (pSoldier->ubID == 0 && FindObj(pSoldier, LETTER) == NO_SLOT) {
				CreateSpecialItem(pSoldier, LETTER);
			}
		}

		// charge the player
		AddTransactionToPlayersBook(IMP_PROFILE, profile, GetWorldTotalMin(), -COST_OF_PROFILE);
		AddHistoryToPlayersLog(HISTORY_CHARACTER_GENERATED, 0, GetWorldTotalMin(), SGPSector(-1, -1));

		fButtonPendingFlag = TRUE;
		iCurrentImpPage = IMP_HOME_PAGE;

		// send email notice, naming the character it reports on: by the time the mail
		// arrives a further I.M.P. may be the one iVoiceId points at
		if (GCM->getIMPPolicy()->sendsProfileResultsEmail())
		{
			AddFutureDayStrategicEvent(EVENT_DAY2_ADD_EMAIL_FROM_IMP, 60 * 7, profile, 2);
		}

		ResetCharacterStats();

		//Display a popup msg box telling the user when and where the merc will arrive
		//DisplayPopUpBoxExplainingMercArrivalLocationAndTime();

		//reset the id of the last merc so we dont get the DisplayPopUpBoxExplainingMercArrivalLocationAndTime() pop up box in another screen by accident
		LaptopSaveInfo.sLastHiredMerc.iIdOfMerc = -1;
	}
}


// fixed? by CJC Nov 28 2002
static void BtnIMPConfirmNo(GUI_BUTTON *btn, UINT32 reason)
{
	if (reason & MSYS_CALLBACK_REASON_POINTER_UP)
	{
		iCurrentImpPage = IMP_FINISH;
	}
}

static INT32 FirstFreeBigEnoughPocket(MERCPROFILESTRUCT const& p, UINT16 const usItem)
{
	UINT32 uiPos;
	// if it fits into a small pocket
	if (GCM->getItem(usItem)->getPerPocket() != 0)
	{
		// check small pockets first
		for (uiPos = SMALLPOCK1POS; uiPos <= SMALLPOCK8POS; uiPos++)
		{
			if (p.inv[uiPos] == NONE)
			{
				return(uiPos);
			}
		}
	}

	// check large pockets
	for (uiPos = BIGPOCK1POS; uiPos <= BIGPOCK4POS; uiPos++)
	{
		if (p.inv[uiPos] == NONE)
		{
			return(uiPos);
		}
	}
	return(-1);
}

static void GiveItemsToPC(UINT8 ubProfileId)
{
	// gives starting items to merc
	// NOTE: Any guns should probably be from those available in regular gun set

	MERCPROFILESTRUCT& p = GetProfile(ubProfileId);

	for (const IMPStartingItemSet& set : GCM->getIMPPolicy()->getInventory()) {
		if (!set.Evaluate(p)) continue;
		for (const ItemModel* item : set.items) {
			UINT32 uiPos;
			if (set.slot) {
				uiPos = *set.slot;
			} else {
				INT32 const iSlot = FirstFreeBigEnoughPocket(p, item->getItemIndex());
				if (iSlot == -1) continue;
				uiPos = iSlot;
			}
			p.inv[uiPos] = item->getItemIndex();
			p.bInvStatus[uiPos] = 100;
			p.bInvNumber[uiPos] = 1;
		}
	}
}

void ResetIMPCharactersEyesAndMouthOffsets(const UINT8 ubMercProfileID)
{
	if (ubMercProfileID >= PROF_HUMMER) return;

	MERCPROFILESTRUCT& p = GetProfile(ubMercProfileID);
	INT32 const iPortrait = FindIMPPortraitByFace(p.ubFaceIndex);
	if (iPortrait < 0) return;

	IMPPortrait const& portrait = GetIMPPortraits()[iPortrait];
	p.usEyesX  = portrait.eyesX;
	p.usEyesY  = portrait.eyesY;
	p.usMouthX = portrait.mouthX;
	p.usMouthY = portrait.mouthY;
}


// ---- the native laptop (Phase 6, LaptopNative.h) ---------------------------------------------------------------
#include "LaptopNative.h"
#include "IMP_Compile_Character.h"
#include "Sound_Control.h"
extern INT32 iCurrentVoices; // IMP_Voices.cc

void ImpNativeCompileQuiz(std::vector<int> const& answers); // IMP_Personality_Quiz.cc
void ImpNativeSetAttributes(int const (&attrs)[10]);       // IMP_Attribute_Selection.cc

namespace LaptopNative
{

bool ImpCanCreate() { return CanCreateAnotherIMPCharacter(); }
bool ImpCodeValid(ST::string const& code) { return GCM->getIMPPolicy()->isCodeAccepted(code); }
int  ImpCost() { return COST_OF_PROFILE; }
bool ImpPicksSkillsDirectly() { return gamepolicy(imp_pick_skills_directly); }

std::vector<ImpPortrait> ImpPortraits(bool const female)
{
	std::vector<ImpPortrait> r;
	for (INT32 i = 0; i < GetNumberOfIMPPortraits(!female); ++i)
	{
		INT32 const idx = GetIMPPortraitIndex(!female, i);
		r.push_back({ i, GetIMPPortraits()[idx].face, female });
	}
	return r;
}

std::vector<ImpVoice> ImpVoices(bool const female)
{
	std::vector<ImpVoice> r;
	for (INT32 i = 0; i < GetNumberOfIMPVoices(!female); ++i)
	{
		INT32 const idx = GetIMPVoiceIndex(!female, i);
		r.push_back({ i, GetIMPVoices()[idx].profile, female });
	}
	return r;
}

void ImpPlayVoice(int const nth)
{
	INT32 const idx = GetIMPVoiceIndex(fCharacterIsMale, nth);
	if (idx < 0) return;
	ST::string const file = ST::format(SPEECHDIR "/{03d}_001.wav", GetIMPVoices()[idx].profile);
	PlayJA2SampleFromFile(file.c_str(), MIDVOLUME, 1, MIDDLEPAN);
}

std::vector<ST::string> ImpSkillNames()
{
	std::vector<ST::string> r;
	for (int i = 0; i < NUM_SKILLTRAITS; ++i) r.push_back(gzMercSkillText[i]);
	return r;
}

ImpResult ImpCreate(ImpChoices const& c)
{
	// the I.M.P. flow from Begin to Confirm, with the choices made on the native pages
	if (!CanCreateAnotherIMPCharacter()) return ImpResult::Invalid;
	if (c.fullName.empty() || c.nickName.empty()) return ImpResult::Invalid;
	if (LaptopSaveInfo.iCurrentBalance < COST_OF_PROFILE) return ImpResult::NoFunds;

	ResetSkillsAttributesAndPersonality();
	pFullName = c.fullName;
	pNickName = c.nickName;
	fCharacterIsMale = !c.female;
	if (gamepolicy(imp_pick_skills_directly))
	{
		for (int const s : c.skills) AddSkillToSkillList(INT8(s));
	}
	else
	{
		ImpNativeCompileQuiz(c.answers); // CompileQuestionsInStatsAndWhatNot, at the end of the quiz
	}
	CreatePlayersPersonalitySkillsAndAttitude(); // the personality finish page
	ImpNativeSetAttributes(c.attrs);             // the attribute finish page
	iPortraitNumber = GetIMPPortraitIndex(fCharacterIsMale, c.portrait);
	iCurrentVoices = c.voice;
	LaptopSaveInfo.iVoiceId = GetIMPVoiceIndex(fCharacterIsMale, c.voice);
	CreateACharacterFromPlayerEnteredStats(); // the finish page's Done

	// BtnIMPConfirmYes
	ProfileID const profile = GetIMPSlotInProgress();
	if (!AddCharacterToPlayersTeam()) return ImpResult::Invalid;
	MarkIMPCharacterCreated(profile);
	AddTransactionToPlayersBook(IMP_PROFILE, profile, GetWorldTotalMin(), -COST_OF_PROFILE);
	AddHistoryToPlayersLog(HISTORY_CHARACTER_GENERATED, 0, GetWorldTotalMin(), SGPSector(-1, -1));
	iCurrentImpPage = IMP_HOME_PAGE;
	if (GCM->getIMPPolicy()->sendsProfileResultsEmail())
	{
		AddFutureDayStrategicEvent(EVENT_DAY2_ADD_EMAIL_FROM_IMP, 60 * 7, profile, 2);
	}
	ResetCharacterStats();
	LaptopSaveInfo.sLastHiredMerc.iIdOfMerc = -1;
	return ImpResult::Created;
}

}
