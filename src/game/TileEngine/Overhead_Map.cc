#include "Overhead_Map.h"
#include "OverheadAdapter.h"
#include "Button_System.h"
#include "ContentManager.h"
#include "Cursors.h"
#include "Directories.h"
#include "Faces.h"
#include "Font.h"
#include "Font_Control.h"
#include "Game_Clock.h"
#include "GameInstance.h"
#include "GameLoop.h"
#include "GameMode.h"
#include "Handle_Items.h"
#include "Handle_UI.h"
#include "HImage.h"
#include "Input.h"
#include "Interface.h"
#include "Interface_Control.h"
#include "Interface_Items.h"
#include "Interface_Panels.h"
#include "Isometric_Utils.h"
#include "LightEffects.h"
#include "Line.h"
#include "Map_Information.h"
#include "MouseSystem.h"
#include "Object_Cache.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Radar_Screen.h"
#include "Render_Dirty.h"
#include "RenderWorld.h"
#include "SmokeEffects.h"
#include "Soldier_Control.h"
#include "Soldier_Init_List.h"
#include "Structure.h"
#include "Structure_Internals.h"
#include "Sys_Globals.h"
#include "SysUtil.h"
#include "Tactical_Placement_GUI.h"
#include "Tile_Surface.h"
#include "TileDat.h"
#include "TileDef.h"
#include "UILayout.h"
#include "Video.h"
#include "VObject.h"
#include "VObject_Blitters.h"
#include "VSurface.h"
#include "World_Items.h"
#include "WorldDef.h"
#include "WorldPipeline.h"
#include <algorithm>
#include <string_theory/string>

/** Top of the overhead map area: centred in the standard box, except for the tactical placement GUI, which sits at the bottom of the screen. */
#define OVERHEAD_Y (gfTacticalPlacementGUIActive ? (INT32)(SCREEN_HEIGHT - 480) : (INT32)STD_SCREEN_Y)

extern SOLDIERINITNODE *gpSelected;

// OK, these are values that are calculated in InitRenderParams( ) with normal view settings.
// These would be different if we change ANYTHING about the game worlkd map sizes...
#define NORMAL_MAP_SCREEN_WIDTH		3160
#define NORMAL_MAP_SCREEN_HEIGHT	1540
#define NORMAL_MAP_SCREEN_X		1580
#define NORMAL_MAP_SCREEN_BY		2400
#define NORMAL_MAP_SCREEN_TY		860

namespace {
cache_key_t const uiOVERMAP{ INTERFACEDIR "/map_bord.sti" };
cache_key_t const uiPERSONS{ INTERFACEDIR "/persons.sti" };


struct SMALL_TILE_SURF
{
	HVOBJECT vo;
};

struct SMALL_TILE_DB
{
	HVOBJECT	vo;
	UINT16		usSubIndex;
};
}

static SMALL_TILE_SURF gSmTileSurf[NUMBEROFTILETYPES];
static SMALL_TILE_DB   gSmTileDB[NUMBEROFTILES];
static TileSetID       gubSmTileNum                   = TILESET_INVALID;
static BOOLEAN         gfInOverheadMap = FALSE;
static bool            gfNativeOverhead = false; // the native view (OverheadAdapter.cc) instead of the legacy one
static MOUSE_REGION    OverheadRegion;
static MOUSE_REGION    OverheadBackgroundRegion;
BOOLEAN                gfOverheadMapDirty             = FALSE;
extern BOOLEAN		gfRadarCurrentGuyFlash;
static INT16           gsStartRestrictedX;
static INT16           gsStartRestrictedY;
static INT16           gsOveritemPoolGridNo           = NOWHERE;


static void CopyOverheadDBShadetablesFromTileset(void);


void InitNewOverheadDB(TileSetID const ubTilesetID)
{
	if (gubSmTileNum == ubTilesetID) return;
	TrashOverheadMap();

	for (UINT32 i = 0; i < NUMBEROFTILETYPES; ++i)
	{
		auto res = GetAdjustedTilesetResource(ubTilesetID, i, "t/");
		SGPVObject* vo;
		try
		{
			vo = AddVideoObjectFromFile(res.resourceFileName);
		}
		catch (std::exception &e)
		{
			SLOGD("{}", e.what());
			// Load one we know about
			vo = AddVideoObjectFromFile(GCM->getTilesetResourceName(GetDefaultTileset(), "t/grass.sti"));
		}

		gSmTileSurf[i].vo = vo;
	}

	// Create database
	UINT32 dbSize = 0;
	for (UINT32 i = 0; i < NUMBEROFTILETYPES; ++i)
	{
		SGPVObject* const vo = gSmTileSurf[i].vo;

		// Get number of regions and check for overflow
		UINT32 const NumRegions = std::min(vo->SubregionCount(), gNumTilesPerType[i]);

		UINT32 k = 0;
		for (; k < NumRegions; ++k)
		{
			gSmTileDB[dbSize].vo         = vo;
			gSmTileDB[dbSize].usSubIndex = k;
			++dbSize;
		}

		// Handle underflow
		for (; k < gNumTilesPerType[i]; ++k)
		{
			gSmTileDB[dbSize].vo         = vo;
			gSmTileDB[dbSize].usSubIndex = 0;
			++dbSize;
		}
	}

	gsStartRestrictedX = 0;
	gsStartRestrictedY = 0;

	// Calculate Scale factors because of restricted map scroll regions
	if (gMapInformation.ubRestrictedScrollID != 0)
	{
		INT16 sX1;
		INT16 sY1;
		INT16 sX2;
		INT16 sY2;
		CalculateRestrictedMapCoords(NORTH, &sX1, &sY1, &sX2, &gsStartRestrictedY, SCREEN_WIDTH, 320);
		CalculateRestrictedMapCoords(WEST,  &sX1, &sY1, &gsStartRestrictedX, &sY2, SCREEN_WIDTH, 320);
	}

	// Copy over shade tables from main tileset
	CopyOverheadDBShadetablesFromTileset();
	gubSmTileNum = ubTilesetID;
}


static ITEM_POOL const* GetClosestItemPool(INT16 const sweet_gridno, UINT8 const radius, INT8 const level)
{
	ITEM_POOL const* closest_item_pool = 0;
	INT32            lowest_range      = 999999;
	for (INT16 y = -radius; y <= radius; ++y)
	{
		INT32 const leftmost = (sweet_gridno + WORLD_COLS * y) / WORLD_COLS * WORLD_COLS;
		for (INT16 x = -radius; x <= radius; ++x)
		{
			INT16 const gridno = sweet_gridno + WORLD_COLS * y + x;
			if (gridno < 0        || WORLD_MAX             <= gridno) continue;
			if (gridno < leftmost || leftmost + WORLD_COLS <= gridno) continue;

			ITEM_POOL const* item_pool = GetItemPool(gridno, level);
			if (!item_pool) continue;

			INT32 const range = GetRangeInCellCoordsFromGridNoDiff(sweet_gridno, gridno);
			if (lowest_range <= range) continue;

			lowest_range      = range;
			closest_item_pool = item_pool;
		}
	}
	return closest_item_pool;
}


static SOLDIERTYPE* GetClosestMercInOverheadMap(INT16 const sweet_gridno, UINT8 const radius)
{
	SOLDIERTYPE* res          = 0;
	INT32        lowest_range = 999999;
	for (INT16 y = -radius; y <= radius; ++y)
	{
		INT32 const leftmost = (sweet_gridno + WORLD_COLS * y) / WORLD_COLS * WORLD_COLS;
		for (INT16 x = -radius; x <= radius; ++x)
		{
			INT16 const gridno = sweet_gridno + WORLD_COLS * y + x;
			if (gridno  < 0       || WORLD_MAX             <= gridno) continue;
			if (gridno < leftmost || leftmost + WORLD_COLS <= gridno) continue;

			// Go on sweet stop
			LEVELNODE const* const l = gpWorldLevelData[gridno].pMercHead;
			if (!l) continue;
			SOLDIERTYPE* const s = l->pSoldier;
			if (!l || s->bVisible == -1) continue;

			INT32 const range = GetRangeInCellCoordsFromGridNoDiff(sweet_gridno, gridno);
			if (lowest_range <= range) continue;

			lowest_range = range;
			res          = s;
		}
	}
	return res;
}


static INT16 GetOffsetLandHeight(INT32 const gridno)
{
	return gpWorldLevelData[gridno].sHeight;
}


static void GetOverheadScreenXYFromGridNo(INT16 const gridno, INT16* const out_x, INT16* const out_y)
{
	GetAbsoluteScreenXYFromMapPos(gridno, out_x, out_y);
	INT16 x = *out_x / 5;
	INT16 y = *out_y / 5;

	x += gsStartRestrictedX + 5;
	y += gsStartRestrictedY + 5;

	y -= GetOffsetLandHeight(gridno) / 5;
	y += gsRenderHeight / 5;

	*out_x = x;
	*out_y = y;
}


void OverheadPointOfGridNo(GridNo const gridno, int* const out_x, int* const out_y)
{
	INT16 x, y;
	GetOverheadScreenXYFromGridNo(INT16(gridno), &x, &y);
	*out_x = x;
	*out_y = y;
}


bool OverheadIsNative(void)
{
	return gfInOverheadMap && gfNativeOverhead;
}


static void DisplayMercNameInOverhead(SOLDIERTYPE const& s)
{
	// Get Screen position of guy
	INT16 x;
	INT16 y;
	GetOverheadScreenXYFromGridNo(s.sGridNo, &x, &y);

	x += STD_SCREEN_X;
	y += OVERHEAD_Y;

	y -= s.sHeightAdjustment / 5 + 13;

	INT16 sX;
	INT16 sY;
	SetFontAttributes(TINYFONT1, FONT_MCOLOR_WHITE);
	FindFontCenterCoordinates(x, y, 1, 1, s.name, TINYFONT1, &sX, &sY);
	GDirtyPrint(sX, sY, s.name);
}


static GridNo GetOverheadMouseGridNoForFullSoldiersGridNo(void);
static void   HandleOverheadUI(void);
static void   RenderOverheadOverlays(void);


void HandleOverheadMap(void)
{
	gfInOverheadMap      = TRUE;
	gsOveritemPoolGridNo = NOWHERE;

	InitNewOverheadDB(giCurrentTilesetID);

	if (gfNativeOverhead)
	{
		// the native view draws and takes the mouse; this is only game flow and the keys
		if (!gfEditMode && gfTacticalPlacementGUIActive)
		{
			DecaySmokeEffects(GetWorldTotalSeconds(), false);
			DecayLightEffects(GetWorldTotalSeconds(), false);
		}
		HandleTalkingAutoFaces();
		HandleNativeOverhead();
		return;
	}

	RestoreBackgroundRects();

	// clear for broken saves before TrashWorld took care of this
	if (!gfEditMode && gfTacticalPlacementGUIActive)
	{
		DecaySmokeEffects(GetWorldTotalSeconds(), false);
		DecayLightEffects(GetWorldTotalSeconds(), false);
	}

	RenderOverheadMap(0, WORLD_COLS / 2, STD_SCREEN_X, OVERHEAD_Y, STD_SCREEN_X + 640, OVERHEAD_Y + 320, FALSE);

	HandleTalkingAutoFaces();

	if (!gfEditMode)
	{
		if (gfTacticalPlacementGUIActive)
		{
			TacticalPlacementHandle();
			if (!gfTacticalPlacementGUIActive) return;
		}
		else
		{
			HandleOverheadUI();

			if (!gfInOverheadMap) return;
			RenderTacticalInterface();
			RenderRadarScreen();
			RenderClock();
			RenderTownIDString();

			HandleAutoFaces();
		}
	}

	if (!gfEditMode && !gfTacticalPlacementGUIActive)
	{
		HandleAnyMercInSquadHasCompatibleStuff(NULL);

		INT16 const usMapPos = GetOverheadMouseGridNo();
		if (usMapPos != NOWHERE)
		{
			const ITEM_POOL* pItemPool;

			// ATE: Find the closest item pool within 5 tiles....
			pItemPool = GetClosestItemPool(usMapPos, 1, 0);
			if (pItemPool != NULL)
			{
				const STRUCTURE* const structure = FindStructure(usMapPos, STRUCTURE_HASITEMONTOP | STRUCTURE_OPENABLE);
				INT8             const bZLevel   = GetZLevelOfItemPoolGivenStructure(usMapPos, 0, structure);
				if (AnyItemsVisibleOnLevel(pItemPool, bZLevel))
				{
					DrawItemPoolList(pItemPool, bZLevel, gusMouseXPos, gusMouseYPos);
					gsOveritemPoolGridNo = GetWorldItem(pItemPool->iItemIndex).sGridNo;
				}
			}

			pItemPool = GetClosestItemPool(usMapPos, 1, 1);
			if (pItemPool != NULL)
			{
				const INT8 bZLevel = 0;
				if (AnyItemsVisibleOnLevel(pItemPool, bZLevel))
				{
					DrawItemPoolList(pItemPool, bZLevel, gusMouseXPos, gusMouseYPos - 5);
					gsOveritemPoolGridNo = GetWorldItem(pItemPool->iItemIndex).sGridNo;
				}
			}
		}
	}

	RenderOverheadOverlays();

	if (!gfEditMode && !gfTacticalPlacementGUIActive)
	{
		const SOLDIERTYPE* const sel = GetSelectedMan();
		if (sel != NULL) DisplayMercNameInOverhead(*sel);

		gSelectedGuy = NULL;
		INT16 const usMapPos = GetOverheadMouseGridNoForFullSoldiersGridNo();
		if (usMapPos != NOWHERE)
		{
			SOLDIERTYPE* const s = GetClosestMercInOverheadMap(usMapPos, 1);
			if (s != NULL)
			{
				if (s->bTeam == OUR_TEAM) gSelectedGuy = s;
				DisplayMercNameInOverhead(*s);
			}
		}
	}

	RenderButtons();
	SaveBackgroundRects();
	RenderFastHelp();
	fInterfacePanelDirty = DIRTYLEVEL0;
}


BOOLEAN InOverheadMap( )
{
	return( gfInOverheadMap );
}


static void ClickOverheadRegionCallbackPrimary(MOUSE_REGION* reg, UINT32 reason);
static void ClickOverheadRegionCallbackSecondary(MOUSE_REGION* reg, UINT32 reason);


void GoIntoOverheadMap( )
{
	gfInOverheadMap = TRUE;

	// The native HUD is up: the overhead is a native view (no legacy regions, picture or panel changes)
	if (!gfEditMode && NativeOverheadWanted())
	{
		gfNativeOverhead = true;
		gfOverheadMapDirty = TRUE;
		OpenNativeOverhead(gfTacticalPlacementGUIActive != FALSE);
		return;
	}
	gfNativeOverhead = false;

	MSYS_DefineRegion(&OverheadBackgroundRegion, STD_SCREEN_X, OVERHEAD_Y, STD_SCREEN_X + 640, OVERHEAD_Y + 360, MSYS_PRIORITY_HIGH, CURSOR_NORMAL, MSYS_NO_CALLBACK, MSYS_NO_CALLBACK);

	MSYS_DefineRegion(&OverheadRegion, STD_SCREEN_X, OVERHEAD_Y, STD_SCREEN_X + 640, OVERHEAD_Y + 320, MSYS_PRIORITY_HIGH, CURSOR_NORMAL, MSYS_NO_CALLBACK, MouseCallbackPrimarySecondary(ClickOverheadRegionCallbackPrimary, ClickOverheadRegionCallbackSecondary));

	// Add shades to persons....
	SGPVObject*            const vo  = GetVObject(uiPERSONS);
	SGPPaletteEntry const* const pal = vo->Palette();
	vo->pShades[0] = Create16BPPPaletteShaded(pal, 256, 256, 256, FALSE);
	vo->pShades[1] = Create16BPPPaletteShaded(pal, 310, 310, 310, FALSE);
	vo->pShades[2] = Create16BPPPaletteShaded(pal,   0,   0,   0, FALSE);

	gfOverheadMapDirty = TRUE;

	if( !gfEditMode )
	{
		// Make sure we are in team panel mode...
		SetNewPanel(0);
		fInterfacePanelDirty = DIRTYLEVEL2;

		// Disable tactical buttons......
		if( !gfEnterTacticalPlacementGUI )
		{
			// Handle switch of panel....
			HandleTacticalPanelSwitch( );
			DisableTacticalTeamPanelButtons( TRUE );
		}

		EmptyBackgroundRects( );
	}

}


static void HandleOverheadUI(void)
{
	InputAtom a;
	while (DequeueSpecificEvent(&a, KEYBOARD_EVENTS))
	{
		if (a.usEvent == KEY_DOWN)
		{
			switch (a.usParam)
			{
				case SDLK_ESCAPE:
				case SDLK_INSERT:
					KillOverheadMap();
					break;

				case 'x':
					if (a.usKeyState & ALT_DOWN)
					{
						HandleShortCutExitState();
					}
					break;
			}
		}
	}
}


void KillOverheadMap()
{
	gfInOverheadMap = FALSE;
	SetRenderFlags( RENDER_FLAG_FULL );
	RenderWorld( );

	if (gfNativeOverhead)
	{
		gfNativeOverhead = false;
		CloseNativeOverhead();
		return;
	}

	MSYS_RemoveRegion(&OverheadRegion );
	MSYS_RemoveRegion(&OverheadBackgroundRegion );

	RemoveVObject(uiOVERMAP);
	RemoveVObject(uiPERSONS);

	HandleTacticalPanelSwitch( );
	DisableTacticalTeamPanelButtons( FALSE );

}


static INT16 GetModifiedOffsetLandHeight(INT32 const gridno)
{
	INT16 const h     = GetOffsetLandHeight(gridno);
	INT16 const mod_h = (h / 80 - 1) * 80;
	return mod_h < 0 ? 0 : mod_h;
}


// The overhead traversal: the small tiles of the sector in the order the legacy renderer blits them. The sink says
// what a blit is (the legacy blitters into the frame buffer, or a recorded instance for the native picture), so the
// traversal itself exists once.
template<typename Sink>
static void TraverseOverhead(Sink& out, INT16 const sStartPointX_M, INT16 const sStartPointY_M, INT16 const sStartPointX_S, INT16 const sStartPointY_S, INT16 const sEndXS, INT16 const sEndYS)
{
	{ // Begin Render Loop
		INT16 sAnchorPosX_M = sStartPointX_M;
		INT16 sAnchorPosY_M = sStartPointY_M;
		INT16 sAnchorPosX_S = sStartPointX_S;
		INT16 sAnchorPosY_S = sStartPointY_S;
		bool  bXOddFlag     = false;
		do
		{
			INT16 sTempPosX_M = sAnchorPosX_M;
			INT16 sTempPosY_M = sAnchorPosY_M;
			INT16 sTempPosX_S = sAnchorPosX_S;
			INT16 sTempPosY_S = sAnchorPosY_S;
			if (bXOddFlag) sTempPosX_S += 4;
			do
			{
				UINT32 const usTileIndex = FASTMAPROWCOLTOPOS(sTempPosY_M, sTempPosX_M);
				if (usTileIndex < GRIDSIZE)
				{
					INT16 const sHeight = GetOffsetLandHeight(usTileIndex) / 5;
					for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pLandStart; n; n = n->pPrevNode)
					{
						SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
						INT16         const  sX    = sTempPosX_S;
						INT16         const  sY    = sTempPosY_S - sHeight + gsRenderHeight / 5;
						pTile.vo->CurrentShade(n->ubShadeLevel);
						out.Trans(pTile, sX, sY);
					}
				}

				sTempPosX_S += 8;
				++sTempPosX_M;
				--sTempPosY_M;
			}
			while (sTempPosX_S < sEndXS);

			if (bXOddFlag)
			{
				++sAnchorPosY_M;
			}
			else
			{
				++sAnchorPosX_M;
			}

			bXOddFlag = !bXOddFlag;
			sAnchorPosY_S += 2;
		}
		while (sAnchorPosY_S < sEndYS);
	}

	{ // Begin Render Loop
		INT16 sAnchorPosX_M = sStartPointX_M;
		INT16 sAnchorPosY_M = sStartPointY_M;
		INT16 sAnchorPosX_S = sStartPointX_S;
		INT16 sAnchorPosY_S = sStartPointY_S;
		bool  bXOddFlag     = false;
		do
		{
			INT16 sTempPosX_M = sAnchorPosX_M;
			INT16 sTempPosY_M = sAnchorPosY_M;
			INT16 sTempPosX_S = sAnchorPosX_S;
			INT16 sTempPosY_S = sAnchorPosY_S;
			if (bXOddFlag) sTempPosX_S += 4;
			do
			{
				UINT32 const usTileIndex = FASTMAPROWCOLTOPOS(sTempPosY_M, sTempPosX_M);
				if (usTileIndex < GRIDSIZE)
				{
					INT16 const sHeight         = GetOffsetLandHeight(usTileIndex) / 5;
					INT16 const sModifiedHeight = GetModifiedOffsetLandHeight(usTileIndex) / 5;

					for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pObjectHead; n; n = n->pNext)
					{
						if (n->usIndex >= NUMBEROFTILES) continue;
						// Don't render itempools!
						if (n->uiFlags & LEVELNODE_ITEM) continue;

						SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
						INT16         const  sX    = sTempPosX_S;
						INT16                sY    = sTempPosY_S;

						if (gTileDatabase[n->usIndex].uiFlags & IGNORE_WORLD_HEIGHT)
						{
							sY -= sModifiedHeight;
						}
						else
						{
							sY -= sHeight;
						}

						sY += gsRenderHeight / 5;

						pTile.vo->CurrentShade(n->ubShadeLevel);
						out.Trans(pTile, sX, sY);
					}

					for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pShadowHead; n; n = n->pNext)
					{
						if (n->usIndex >= NUMBEROFTILES) continue;

						SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
						INT16         const  sX    = sTempPosX_S;
						INT16                sY    = sTempPosY_S - sHeight;

						sY += gsRenderHeight / 5;

						pTile.vo->CurrentShade(n->ubShadeLevel);
						out.Shadow(pTile, sX, sY);
					}

					for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pStructHead; n; n = n->pNext)
					{
						if (n->usIndex >= NUMBEROFTILES) continue;
						// Don't render itempools!
						if (n->uiFlags & LEVELNODE_ITEM) continue;

						SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
						INT16         const  sX    = sTempPosX_S;
						INT16                sY    = sTempPosY_S;

						if (gTileDatabase[n->usIndex].uiFlags & IGNORE_WORLD_HEIGHT)
						{
							sY -= sModifiedHeight;
						}
						else
						{
							sY -= sHeight;
						}

						sY += gsRenderHeight / 5;

						pTile.vo->CurrentShade(n->ubShadeLevel);
						out.Trans(pTile, sX, sY);
					}
				}

				sTempPosX_S += 8;
				++sTempPosX_M;
				--sTempPosY_M;
			}
			while (sTempPosX_S < sEndXS);

			if (bXOddFlag)
			{
				++sAnchorPosY_M;
			}
			else
			{
				++sAnchorPosX_M;
			}

			bXOddFlag = !bXOddFlag;
			sAnchorPosY_S += 2;
		}
		while (sAnchorPosY_S < sEndYS);
	}

	{ // ROOF RENDR LOOP
		// Begin Render Loop
		INT16 sAnchorPosX_M = sStartPointX_M;
		INT16 sAnchorPosY_M = sStartPointY_M;
		INT16 sAnchorPosX_S = sStartPointX_S;
		INT16 sAnchorPosY_S = sStartPointY_S;
		bool  bXOddFlag     = false;
		do
		{
			INT16 sTempPosX_M = sAnchorPosX_M;
			INT16 sTempPosY_M = sAnchorPosY_M;
			INT16 sTempPosX_S = sAnchorPosX_S;
			INT16 sTempPosY_S = sAnchorPosY_S;
			if (bXOddFlag) sTempPosX_S += 4;
			do
			{
				UINT32 const usTileIndex = FASTMAPROWCOLTOPOS(sTempPosY_M, sTempPosX_M);
				if (usTileIndex < GRIDSIZE)
				{
					INT16 const sHeight = GetOffsetLandHeight(usTileIndex) / 5;

					for (LEVELNODE const* n = gpWorldLevelData[usTileIndex].pRoofHead; n; n = n->pNext)
					{
						if (n->usIndex >= NUMBEROFTILES)   continue;
						if (n->uiFlags & LEVELNODE_HIDDEN) continue;

						SMALL_TILE_DB const& pTile = gSmTileDB[n->usIndex];
						INT16         const  sX    = sTempPosX_S;
						INT16                sY    = sTempPosY_S - sHeight;

						sY -= WALL_HEIGHT / 5;
						sY += gsRenderHeight / 5;

						pTile.vo->CurrentShade(n->ubShadeLevel);

						// RENDER!
						out.Trans(pTile, sX, sY);
					}
				}

				sTempPosX_S += 8;
				++sTempPosX_M;
				--sTempPosY_M;
			}
			while (sTempPosX_S < sEndXS);

			if (bXOddFlag)
			{
				++sAnchorPosY_M;
			}
			else
			{
				++sAnchorPosX_M;
			}

			bXOddFlag = !bXOddFlag;
			sAnchorPosY_S += 2;
		}
		while (sAnchorPosY_S < sEndYS);
	}
}


/** The legacy blitters, into the 16-bit frame buffer. */
struct FrameBufferSink
{
	UINT16* buf;
	UINT32  pitch;
	void Trans(SMALL_TILE_DB const& t, INT16 const x, INT16 const y) const
	{
		Blt8BPPDataTo16BPPBufferTransparent(buf, pitch, t.vo, x, y, t.usSubIndex);
	}
	void Shadow(SMALL_TILE_DB const& t, INT16 const x, INT16 const y) const
	{
		Blt8BPPDataTo16BPPBufferShadow(buf, pitch, t.vo, x, y, t.usSubIndex);
	}
};


/** The native picture: each blit is an instance of the world pipeline (WorldPipe), rasterized once at the end in
 * 24-bit colour. */
struct PipeSink
{
	WorldPipe::Frame&      frame;
	WorldPipe::SpritePool& pool;

	void Add(SMALL_TILE_DB const& t, INT16 const x, INT16 const y, WorldPipe::Op const op) const
	{
		SGPVObject* const vo = t.vo;
		ETRLEObject const& e = vo->SubregionProperties(t.usSubIndex);
		int const ox = x + e.sOffsetX, oy = y + e.sOffsetY;
		int const x0 = std::max(ox, 0), y0 = std::max(oy, 0);
		int const x1 = std::min(ox + int(e.usWidth), frame.width), y1 = std::min(oy + int(e.usHeight), frame.height);
		if (x0 >= x1 || y0 >= y1) return;

		WorldPipe::Instance in{};
		in.x0 = UINT16(x0); in.y0 = UINT16(y0); in.x1 = UINT16(x1); in.y1 = UINT16(y1);
		in.ox = INT16(ox);  in.oy = INT16(oy);
		in.op = op;
		in.columns = WorldPipe::NO_COLUMNS;
		WorldPipe::SpritePool::Entry const& sp = pool.Get(vo->PixData(e), e.uiDataLength, e.usWidth, e.usHeight);
		in.sprite = sp.offset;
		in.spriteW = sp.w;
		UINT16 const* const shade = vo->CurrentShade();
		in.palette = frame.Palette(shade, vo->CurrentShade24());
		frame.instances.push_back(in);
	}
	void Trans(SMALL_TILE_DB const& t, INT16 const x, INT16 const y) const { Add(t, x, y, WorldPipe::Op::Transparent); }
	void Shadow(SMALL_TILE_DB const& t, INT16 const x, INT16 const y) const { Add(t, x, y, WorldPipe::Op::Shadow); }
};


void RenderOverheadPicture(std::vector<uint32_t>& rgb, int& w, int& h)
{
	InitNewOverheadDB(giCurrentTilesetID);
	w = OVERHEAD_PICTURE_W;
	h = OVERHEAD_PICTURE_H;

	// the pool keys sprites by the address of their data: a new tileset starts a new one
	static WorldPipe::SpritePool pool;
	static TileSetID poolTileset = TILESET_INVALID;
	if (poolTileset != giCurrentTilesetID)
	{
		pool.Reset();
		poolTileset = giCurrentTilesetID;
	}
	pool.NextFrame();
	WorldPipe::Frame frame;
	frame.Clear(w, h);
	frame.clearColor = 0;
	PipeSink sink{ frame, pool };
	TraverseOverhead(sink, 0, WORLD_COLS / 2, 0, 0, INT16(w), INT16(h));

	WorldPipe::Target target;
	target.Clear(frame);
	WorldPipe::Rasterize(frame, pool, target);
	rgb = std::move(target.color);

	// OK, blacken out edges of smaller maps...
	if (gMapInformation.ubRestrictedScrollID != 0)
	{
		for (UINT8 const dir : { NORTH, WEST, SOUTH, EAST })
		{
			INT16 x1, y1, x2, y2;
			CalculateRestrictedMapCoords(dir, &x1, &y1, &x2, &y2, INT16(w), INT16(h));
			for (int y = std::max<int>(y1, 0); y < std::min<int>(y2, h); ++y)
				for (int x = std::max<int>(x1, 0); x < std::min<int>(x2, w); ++x) rgb[size_t(y) * w + x] = 0;
		}
	}
}


void RenderOverheadMap(INT16 const sStartPointX_M, INT16 const sStartPointY_M, INT16 const sStartPointX_S, INT16 const sStartPointY_S, INT16 const sEndXS, INT16 const sEndYS, BOOLEAN const fFromMapUtility)
{
	if (!gfOverheadMapDirty) return;

	// Black out
	ColorFillVideoSurfaceArea(FRAME_BUFFER, sStartPointX_S, sStartPointY_S, sEndXS,	sEndYS, 0);

	InvalidateScreen();
	gfOverheadMapDirty = FALSE;

	{ SGPVSurface::Lock l(FRAME_BUFFER);
		FrameBufferSink sink{ l.Buffer<UINT16>(), l.Pitch() };
		TraverseOverhead(sink, sStartPointX_M, sStartPointY_M, sStartPointX_S, sStartPointY_S, sEndXS, sEndYS);
	}

	// OK, blacken out edges of smaller maps...
	if (gMapInformation.ubRestrictedScrollID != 0)
	{
		UINT16 const black = Get16BPPColor(FROMRGB(0, 0, 0));
		INT16 sX1;
		INT16 sX2;
		INT16 sY1;
		INT16 sY2;

		// The coordinates are relative to the top-left of the map area; the map is not at 0,0 on wide screens.
		INT16 const w = sEndXS - sStartPointX_S;
		INT16 const h = sEndYS - sStartPointY_S;
		for (UINT8 const dir : { NORTH, WEST, SOUTH, EAST })
		{
			CalculateRestrictedMapCoords(dir, &sX1, &sY1, &sX2, &sY2, w, h);
			ColorFillVideoSurfaceArea(FRAME_BUFFER, sX1 + sStartPointX_S, sY1 + sStartPointY_S, sX2 + sStartPointX_S, sY2 + sStartPointY_S, black);
		}
	}

	if (!fFromMapUtility)
	{ // Render border!
		BltVideoObject(FRAME_BUFFER, uiOVERMAP, 0, STD_SCREEN_X + 0, OVERHEAD_Y + 0);

		// On screens bigger than 640x480 the map sits in the middle: fill the rest of the tactical
		// area around it instead of leaving the world showing through.
		if (g_ui.isBigScreen() && !gfEditMode)
		{
			UINT16 const c      = Get16BPPColor(FROMRGB(20, 15, 10));
			INT32  const left   = STD_SCREEN_X;
			INT32  const top    = OVERHEAD_Y;
			INT32  const right  = STD_SCREEN_X + 640;
			INT32  const bottom = OVERHEAD_Y + (gfTacticalPlacementGUIActive ? 480 : 360);
			INT32  const areaB  = gfTacticalPlacementGUIActive ? SCREEN_HEIGHT : INTERFACE_START_Y;
			if (top > 0)                  ColorFillVideoSurfaceArea(FRAME_BUFFER, 0,     0,      SCREEN_WIDTH, top,    c);
			if (bottom < areaB)           ColorFillVideoSurfaceArea(FRAME_BUFFER, 0,     bottom, SCREEN_WIDTH, areaB,  c);
			if (left > 0)                 ColorFillVideoSurfaceArea(FRAME_BUFFER, 0,     top,    left,         std::min<INT32>(bottom, areaB), c);
			if (right < (INT32)SCREEN_WIDTH) ColorFillVideoSurfaceArea(FRAME_BUFFER, right, top,   SCREEN_WIDTH, std::min<INT32>(bottom, areaB), c);
		}
	}

	// Update the save buffer
	BltVideoSurface(guiSAVEBUFFER, FRAME_BUFFER, 0, 0, NULL);
}


static void RenderOverheadOverlays(void)
{
	SGPVSurface::Lock l(FRAME_BUFFER);
	UINT16* const pDestBuf         = l.Buffer<UINT16>();
	UINT32  const uiDestPitchBYTES = l.Pitch();

	// Soldier overlay
	SGPVObject*        const marker = GetVObject(uiPERSONS);
	SOLDIERTYPE const* const sel    = gfTacticalPlacementGUIActive || !gfRadarCurrentGuyFlash ? 0 : GetSelectedMan();
	UINT16             const end    = gfTacticalPlacementGUIActive ? gTacticalStatus.Team[OUR_TEAM].bLastID + 1 : MAX_NUM_SOLDIERS;
	for (UINT32 i = 0; i < end; ++i)
	{
		SOLDIERTYPE const& s = GetMan(i);
		if (!s.bActive || !s.bInSector) continue;

		if (!gfTacticalPlacementGUIActive && s.bLastRenderVisibleValue == -1 && !(gTacticalStatus.uiFlags & SHOW_ALL_MERCS))
		{
			continue;
		}

		if (s.sGridNo == NOWHERE) continue;

		//Soldier is here.  Calculate his screen position based on his current gridno.
		INT16 sX;
		INT16 sY;
		GetOverheadScreenXYFromGridNo(s.sGridNo, &sX, &sY);
		//Now, draw his "doll"

		sX += STD_SCREEN_X;
		sY += OVERHEAD_Y;

		//adjust for position.
		sX += 2;
		sY -= 5;

		sY -= s.sHeightAdjustment / 5; // Adjust for height

		UINT32 const shade =
			&s == sel           ? 2 :
			s.sHeightAdjustment ? 1 : // On roof
			0;
		marker->CurrentShade(shade);

		if (gfEditMode && GameMode::getInstance()->isEditorMode() && gpSelected && gpSelected->pSoldier == &s)
		{ //editor:  show the selected edited merc as the yellow one.
			Blt8BPPDataTo16BPPBufferTransparent(pDestBuf, uiDestPitchBYTES, marker, sX, sY, 0);
		}
		else
		{
			UINT16 const region =
				!gfTacticalPlacementGUIActive                                ? s.bTeam :
				s.uiStatusFlags & SOLDIER_VEHICLE                            ? 9       :
				&s == gpTacticalPlacementSelectedSoldier                     ? 7       :
				&s == gpTacticalPlacementHilightedSoldier && s.uiStatusFlags ? 8       :
				s.bTeam;
			Blt8BPPDataTo16BPPBufferTransparent(pDestBuf, uiDestPitchBYTES, marker, sX, sY, region);
			ETRLEObject const& e = marker->SubregionProperties(region);
			RegisterBackgroundRect(BGND_FLAG_SINGLE, sX + e.sOffsetX, sY + e.sOffsetY, e.usWidth, e.usHeight);
		}
	}

	// Items overlay
	if (!gfTacticalPlacementGUIActive)
	{
		CFOR_EACH_WORLD_ITEM(wi)
		{
			if (wi.bVisible != VISIBLE && !(gTacticalStatus.uiFlags & SHOW_ALL_ITEMS))
			{
				continue;
			}

			INT16 sX;
			INT16 sY;
			GetOverheadScreenXYFromGridNo(wi.sGridNo, &sX, &sY);

			sX += STD_SCREEN_X;
			sY += OVERHEAD_Y;

			//adjust for position.
			sY += 6;

			UINT32 col;
			if (gsOveritemPoolGridNo == wi.sGridNo)
			{
				col = FROMRGB(255, 0, 0);
			}
			else if (gfRadarCurrentGuyFlash)
			{
				col = FROMRGB(0, 0, 0);
			}
			else switch (wi.bVisible)
			{
				case HIDDEN_ITEM:      col = FROMRGB(  0,   0, 255); break;
				case BURIED:           col = FROMRGB(255,   0,   0); break;
				case HIDDEN_IN_OBJECT: col = FROMRGB(  0,   0, 255); break;
				case INVISIBLE:        col = FROMRGB(  0, 255,   0); break;
				case VISIBLE:          col = FROMRGB(255, 255, 255); break;
				default:               abort();
			}
			PixelDraw(FALSE, sX, sY, Get16BPPColor(col), pDestBuf);
			InvalidateRegion(sX, sY, sX + 1, sY + 1);
		}
	}
}


static void ClickOverheadRegionCallbackPrimary(MOUSE_REGION* reg, UINT32 reason)
{
	if( gfTacticalPlacementGUIActive )
	{
		HandleTacticalPlacementClicksInOverheadMap(reason);
		return;
	}

	// Get new proposed center location.
	const GridNo pos = GetOverheadMouseGridNo();
	INT16 cell_x;
	INT16 cell_y;
	ConvertGridNoToCenterCellXY(pos, &cell_x, &cell_y);

	SetRenderCenter(cell_x, cell_y);

	KillOverheadMap();
}

static void ClickOverheadRegionCallbackSecondary(MOUSE_REGION* reg, UINT32 reason)
{
	if( gfTacticalPlacementGUIActive )
	{
		HandleTacticalPlacementClicksInOverheadMap(reason);
		return;
	}

	KillOverheadMap();
}


/** The tile under a picture pixel (+ @a dy: the legacy nudge). */
static GridNo GridNoAtPicturePoint(int const x, int const y, int const dy)
{
	// ATE: Adjust alogrithm values a tad to reflect map positioning
	INT16 const sWorldScreenX = (x - gsStartRestrictedX -  5) * 5;
	INT16       sWorldScreenY = (y - gsStartRestrictedY + dy) * 5;

	// Get new proposed center location.
	const GridNo grid_no = GetMapPosFromAbsoluteScreenXY(sWorldScreenX, sWorldScreenY);

	// Adjust for height.....
	sWorldScreenY += GetOffsetLandHeight(grid_no);
	sWorldScreenY -= gsRenderHeight;

	return GetMapPosFromAbsoluteScreenXY(sWorldScreenX, sWorldScreenY);
}


GridNo OverheadGridNoAtPoint(int const x, int const y, bool const forSoldier)
{
	if (x < 0 || y < 0 || x >= OVERHEAD_PICTURE_W || y >= OVERHEAD_PICTURE_H) return NOWHERE;
	return GridNoAtPicturePoint(x, y, forSoldier ? 0 : -8);
}


static GridNo InternalGetOverheadMouseGridNo(const INT dy)
{
	if (!(OverheadRegion.uiFlags & MSYS_MOUSE_IN_AREA)) return NOWHERE;
	return GridNoAtPicturePoint(gusMouseXPos - STD_SCREEN_X, gusMouseYPos - OVERHEAD_Y, dy);
}


GridNo GetOverheadMouseGridNo(void)
{
	return InternalGetOverheadMouseGridNo(-8);
}


static GridNo GetOverheadMouseGridNoForFullSoldiersGridNo(void)
{
	return InternalGetOverheadMouseGridNo(0);
}


void CalculateRestrictedMapCoords( INT8 bDirection, INT16 *psX1, INT16 *psY1, INT16 *psX2, INT16 *psY2, INT16 sEndXS, INT16 sEndYS )
{
	switch( bDirection )
	{
		case NORTH:

			*psX1 = 0;
			*psX2 = sEndXS;
			*psY1 = 0;
			*psY2 = std::abs(NORMAL_MAP_SCREEN_TY - gsTopY) / 5;
			break;

		case WEST:

			*psX1 = 0;
			*psX2 = std::abs(-NORMAL_MAP_SCREEN_X - gsLeftX) / 5;
			*psY1 = 0;
			*psY2 = sEndYS;
			break;

		case SOUTH:

			*psX1 = 0;
			*psX2 = sEndXS;
			*psY1 = (NORMAL_MAP_SCREEN_HEIGHT - std::abs(NORMAL_MAP_SCREEN_BY - gsBottomY)) / 5;
			*psY2 = sEndYS;
			break;

		case EAST:

			*psX1 = (NORMAL_MAP_SCREEN_WIDTH - std::abs(NORMAL_MAP_SCREEN_X - gsRightX)) / 5;
			*psX2 = sEndXS;
			*psY1 = 0;
			*psY2 = sEndYS;
			break;

	}
}


static void CopyOverheadDBShadetablesFromTileset(void)
{
	// Loop through tileset
	for (size_t i = 0; i < NUMBEROFTILETYPES; ++i)
	{
		gSmTileSurf[i].vo->ShareShadetables(gTileSurfaceArray[i]->vo);
	}
}


void TrashOverheadMap(void)
{
	if (gubSmTileNum == TILESET_INVALID) return;
	gubSmTileNum = TILESET_INVALID;

	FOR_EACH(SMALL_TILE_SURF, i, gSmTileSurf)
	{
		DeleteVideoObject(i->vo);
	}
}
