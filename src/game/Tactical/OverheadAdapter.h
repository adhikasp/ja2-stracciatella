#pragma once
// The native overhead map's adapter (issue #323): reads the legacy globals (soldiers, world items, the camera) into
// the OverheadModel frame, draws the whole sector's small-tile picture through the world pipeline, and carries out
// the view's clicks. The native view (NativeUI/TacticalOverhead.cc) and Lua (ja2.overhead) read what is here.

#include "OverheadModel.h"

#include <cstdint>
#include <string>
#include <vector>

/** The native HUD is the tactical interface (ui_mode "tactical" resolves to native): the overhead and the
 * placement are native views too. The legacy ones stay for the legacy HUD (deleted with it, #324). */
bool NativeOverheadWanted();

/** The overhead (or the placement, which is a mode of it) opens / closes as a native view. */
void OpenNativeOverhead(bool placement);
void CloseNativeOverhead();
bool NativeOverheadOpen();

/** Once per frame while the overhead is open (GameScreen -> HandleOverheadMap): the keys, then the frame. */
void HandleNativeOverhead();

/** The sector card's minimap: keeps the picture and the frame fresh while the HUD is up and no overhead is open.
 * Returns false when there is no sector loaded to draw. */
bool UpdateNativeMinimap();

OverheadModel::Frame const& CurrentOverheadFrame();

struct OverheadPicture
{
	int                    w = 0, h = 0;
	std::vector<uint32_t>  rgb;      // 0xRRGGBB
	int                    revision = 0; // changes when it was drawn again
	double                 ms = 0;       // how long the last drawing took
};
OverheadPicture const& CurrentOverheadPicture();

// ---- what the view asks for
/** The pointer is over picture pixel (x, y), or off the picture (-1, -1): sets what the hover shows. */
void OverheadHover(int x, int y);
/** A click on picture pixel (x, y). In the overhead: centres the world view there and leaves. In placement: puts
 * the selected merc there. Returns whether it did something. */
bool OverheadClick(int x, int y);
/** Centres the world view on a picture pixel (the sector card's minimap, and the overhead's click). */
bool OverheadCentreOn(int x, int y);
/** Leaves the overhead (not the placement: that ends with Done). */
void OverheadLeave();
