#ifndef __TACTICAL_PLACEMENT_GUI_H
#define __TACTICAL_PLACEMENT_GUI_H

#include "JA2Types.h"
#include "OverheadModel.h"

#include <string>


void InitTacticalPlacementGUI();
void TacticalPlacementHandle(void);

void HandleTacticalPlacementClicksInOverheadMap(INT32 reason);

extern BOOLEAN gfTacticalPlacementGUIActive;
extern BOOLEAN gfEnterTacticalPlacementGUI;

extern SOLDIERTYPE *gpTacticalPlacementSelectedSoldier;
extern SOLDIERTYPE *gpTacticalPlacementHilightedSoldier;

//Saved value.  Contains the last choice for future battles.
extern UINT8	gubDefaultButton;

extern BOOLEAN gfTacticalPlacementGUIDirty;
extern BOOLEAN gfValidLocationsChanged;
extern SGPVObject* giMercPanelImage;

// The native placement (issue #323): the view's operations. The rules are OverheadModel::Placement.
bool NativePlacementActive();
OverheadModel::Placement const& NativePlacementState();
/** Why the last click did nothing (a legacy message text), or empty. */
std::string const& NativePlacementNotice();
/** A click at a picture pixel: puts the selected merc (or his group) there. False: nothing was placed. */
bool NativePlacementClick(int x, int y);
void NativePlacementSelect(int index);
void NativePlacementHover(int index);
void NativePlacementDeselect();
void NativePlacementClear();
void NativePlacementSpread();
void NativePlacementGroup();
/** Finishes the placement at the next frame; false while somebody is not placed. */
bool NativePlacementDone();

#endif
