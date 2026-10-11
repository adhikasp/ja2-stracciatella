#ifndef __OVERHEADMAP_H
#define __OVERHEADMAP_H

#include "JA2Types.h"
#include "Types.h"
#include "World_Tileset_Enums.h"

#include <cstdint>
#include <vector>

/** The overhead picture: the whole sector in small tiles. */
constexpr int OVERHEAD_PICTURE_W = 640;
constexpr int OVERHEAD_PICTURE_H = 320;


void InitNewOverheadDB(TileSetID);
void RenderOverheadMap( INT16 sStartPointX_M, INT16 sStartPointY_M, INT16 sStartPointX_S, INT16 sStartPointY_S, INT16 sEndXS, INT16 sEndYS, BOOLEAN fFromMapUtility );


void HandleOverheadMap(void);
BOOLEAN InOverheadMap(void);
void GoIntoOverheadMap(void);
void KillOverheadMap(void);

void CalculateRestrictedMapCoords( INT8 bDirection, INT16 *psX1, INT16 *psY1, INT16 *psX2, INT16 *psY2, INT16 sEndXS, INT16 sEndYS );

void TrashOverheadMap(void);

GridNo GetOverheadMouseGridNo(void);

/** The native overhead view (issue #323, Tactical/OverheadAdapter.cc): the same small-tile traversal, recorded into
 * the world pipeline and rasterized in 24-bit colour. @a rgb is w x h pixels, 0xRRGGBB. */
void RenderOverheadPicture(std::vector<uint32_t>& rgb, int& w, int& h);
/** The picture pixel the anchor of a tile is at (where the legacy overlay drew its markers, before the screen
 * offset). */
void OverheadPointOfGridNo(GridNo, int* x, int* y);
/** The tile under a picture pixel. @a forSoldier: the point is a soldier's dot (the legacy picked a soldier from
 * a different offset than a place to move the view to). NOWHERE off the map. */
GridNo OverheadGridNoAtPoint(int x, int y, bool forSoldier);
/** The overhead is showing as the native view (no legacy regions, buttons or drawing). */
bool OverheadIsNative(void);

extern BOOLEAN gfOverheadMapDirty;

#endif
