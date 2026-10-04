#pragma once
// Images the native UI takes from the player's game data at runtime (nothing derived from them is written anywhere).

#include <SDL3/SDL.h>

namespace Rml { class ElementDocument; }

#include <functional>
#include <utility>
#include <string>

namespace NativeUI
{
	/** Frame @a frame of an STI as a new RGBA surface (transparent where the image is), or nullptr. */
	SDL_Surface* LoadStiFrame(std::string const& file, int frame);
	/** A whole image (8 or 16 bit STI/PCX, e.g. a load screen) as a new RGBA surface, or nullptr (MockScreen.cc). */
	SDL_Surface* LoadWholeImage(std::string const& file);
	/** @a src (taken over) enlarged 2^passes times with Scale2x, for full-screen art (FrontEndSupport.cc). */
	SDL_Surface* UpliftArt(SDL_Surface* src, int passes);
	/** A save's thumbnail PNG as a new RGBA surface, or nullptr. */
	SDL_Surface* LoadThumbnail(std::string const& path);
	/** A new RGBA surface: @a src cut to (x, y, w, h) and enlarged @a scale times with nearest-neighbour sampling. */
	SDL_Surface* CropScaled(SDL_Surface const* src, int x, int y, int w, int h, int scale);

	/** Images named "<prefix><rest>" come from @a source (called with the whole name). */
	void RegisterImageSource(std::string const& prefix, std::function<SDL_Surface*(std::string const&)> source);
	/** The front-end images: "mainmenu-art", "loadscreen-<id>", "save-thumb-<save>@<time>" (MockScreen.cc). */
	void RegisterFrontEndImages();
	/** The image provider of the native UI: "face-<n>" (a merc's big portrait) and the registered sources. */
	SDL_Surface* ProvideGameImage(std::string const& name);

	/** The Phase 5 mock pictures: "nitem-<item>", "nitembig-<item>", "sface-<face>", each at "@<k>" times (MockWorld.cc). */
	void RegisterTacticalMockImages();
	/** The size of one of those pictures at 1x (0 x 0 if there is none). */
	std::pair<int, int> PictureBaseSize(std::string const& name);
	/** Integer-scales the mock pictures of @a doc and, for data-world="clear", shows the whole tactical world behind it. */
	void PrepareMockDocument(Rml::ElementDocument* doc);
	/** The legacy tactical HUD draws itself again after a mock closed. */
	void RestoreAfterMock();
	/** The laptop images: "shop-item-<index>" (an item's big picture, LaptopImages.cc). */
	void RegisterLaptopImages();
}
