// The native overhead map, the placement and the sector card's minimap (issue #323, docs/plan/native-tactical.md
// "OverheadModel", wireframes W11 and W12).
//
// What is shown is decided elsewhere: Tactical/OverheadAdapter.cc reads the legacy globals into an
// OverheadModel::Frame and draws the whole sector's small-tile picture through the world pipeline;
// TileEngine/Tactical_Placement_GUI.cc carries out the placement. This file puts them on the screen: the picture at
// the largest integer scale that fits, the marks, zones, view box and hover over it in the picture's own pixels, the
// legend and the roster as a view model ("overhead"), and the minimap in the HUD's sector card.
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "OverheadAdapter.h"
#include "OverheadModel.h"
#include "Overhead_Map.h"
#include "Tactical_Placement_GUI.h"
#include "Text.h"
#include "Logger.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace NativeUI
{
namespace
{
	using namespace OverheadModel;

	std::string Num(int const v) { return std::to_string(v); }

	std::string Fill(std::string fmt, std::string const& a, std::string const& b = {})
	{
		auto put = [&fmt](std::string const& v) {
			size_t const i = fmt.find("{}");
			if (i != std::string::npos) fmt.replace(i, 2, v);
		};
		put(a);
		if (!b.empty()) put(b);
		return fmt;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The pictures

	SDL_Surface* PictureSurface()
	{
		OverheadPicture const& p = CurrentOverheadPicture();
		if (p.rgb.empty() || p.w <= 0 || p.h <= 0) return nullptr;
		SDL_Surface* const s = SDL_CreateSurface(p.w, p.h, SDL_PIXELFORMAT_RGBA32);
		if (!s) return nullptr;
		for (int y = 0; y < p.h; ++y)
		{
			auto* d = static_cast<Uint8*>(s->pixels) + size_t(y) * s->pitch;
			for (int x = 0; x < p.w; ++x, d += 4)
			{
				uint32_t const c = p.rgb[size_t(y) * p.w + x];
				d[0] = Uint8(c >> 16); d[1] = Uint8(c >> 8); d[2] = Uint8(c); d[3] = 255;
			}
		}
		return s;
	}

	/** "overhead-map@<k>~<revision>": the picture enlarged k times, nearest-neighbour, never stretched. */
	SDL_Surface* ProvideMap(std::string const& name)
	{
		int k = 1;
		if (auto const at = name.find('@'); at != std::string::npos) k = std::max(1, std::atoi(name.c_str() + at + 1));
		SDL_Surface* const base = PictureSurface();
		if (!base || k == 1) return base;
		SDL_Surface* const s = CropScaled(base, 0, 0, base->w, base->h, k);
		SDL_DestroySurface(base);
		return s;
	}

	/** "overhead-mini@<w>x<h>~<revision>": the picture averaged down to w x h (the sector card). */
	SDL_Surface* ProvideMini(std::string const& name)
	{
		int w = 0, h = 0;
		if (auto const at = name.find('@'); at != std::string::npos) std::sscanf(name.c_str() + at + 1, "%dx%d", &w, &h);
		OverheadPicture const& p = CurrentOverheadPicture();
		if (w <= 0 || h <= 0 || p.rgb.empty()) return nullptr;
		SDL_Surface* const s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
		if (!s) return nullptr;
		for (int y = 0; y < h; ++y)
		{
			auto* d = static_cast<Uint8*>(s->pixels) + size_t(y) * s->pitch;
			int const y0 = y * p.h / h, y1 = std::max(y0 + 1, (y + 1) * p.h / h);
			for (int x = 0; x < w; ++x, d += 4)
			{
				int const x0 = x * p.w / w, x1 = std::max(x0 + 1, (x + 1) * p.w / w);
				unsigned r = 0, g = 0, b = 0, n = 0;
				for (int yy = y0; yy < y1 && yy < p.h; ++yy)
					for (int xx = x0; xx < x1 && xx < p.w; ++xx)
					{
						uint32_t const c = p.rgb[size_t(yy) * p.w + xx];
						r += (c >> 16) & 0xFF; g += (c >> 8) & 0xFF; b += c & 0xFF; ++n;
					}
				if (n == 0) n = 1;
				d[0] = Uint8(r / n); d[1] = Uint8(g / n); d[2] = Uint8(b / n); d[3] = 255;
			}
		}
		return s;
	}

	void RegisterSources()
	{
		static bool done = false;
		if (done) return;
		done = true;
		RegisterImageSource("overhead-map", ProvideMap);
		RegisterImageSource("overhead-mini", ProvideMini);
		RegisterTacticalMockImages(); // the face pictures ("sface-<n>")
	}

	/** A face picture at the largest integer scale that fits maxW x maxH output pixels. */
	std::string FaceSrc(int const profile, float const maxW, float const maxH)
	{
		if (profile < 0) return {};
		std::string const name = "sface-" + Num(profile);
		auto const [w, h] = PictureBaseSize(name);
		if (!w) return {};
		int k = 1;
		while (w * (k + 1) <= maxW && h * (k + 1) <= maxH) ++k;
		return name + "@" + Num(k);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Markup over the picture

	std::string Px(float const v) { return Num(int(std::lround(v))) + "px"; }

	std::string MarkMarkup(Frame const& f, float const k, std::string const& hot)
	{
		std::string out;
		for (Mark const& m : f.marks)
		{
			bool const item = m.kind == MarkKind::Item;
			float const d = item ? 3.f * k + 4.f : 5.f * k + 6.f;
			std::string cls = std::string("mk ") + KindName(m.kind);
			if (m.selected) cls += " sel";
			if (!hot.empty() && hot == KindName(m.kind) + Num(m.id)) cls += " hot";
			out += "<span class=\"" + cls + "\" style=\"left: " + Px(m.x * k - d / 2) + "; top: " + Px(m.y * k - d / 2) +
				"; width: " + Px(d) + "; height: " + Px(d) + ";\"></span>";
		}
		return out;
	}

	std::string ZoneMarkup(Frame const& f, std::vector<Side> const& lit, float const k)
	{
		std::string out;
		if (!f.placement) return out;
		for (Side const s : lit)
		{
			Rect const r = ZoneRect(s);
			out += "<div class=\"zone\" style=\"left: " + Px(r.x * k) + "; top: " + Px(r.y * k) + "; width: " + Px(r.w * k) +
				"; height: " + Px(r.h * k) + ";\"></div>";
		}
		return out;
	}

	std::string ViewMarkup(Frame const& f, float const k, float const pw, float const ph)
	{
		if (!f.view.valid || f.placement) return {};
		float const l = std::clamp(f.view.x * k, 0.f, pw), t = std::clamp(f.view.y * k, 0.f, ph);
		float const r = std::clamp((f.view.x + f.view.w) * k, 0.f, pw), b = std::clamp((f.view.y + f.view.h) * k, 0.f, ph);
		if (r - l < 4 || b - t < 4) return {};
		return "<div class=\"view\" style=\"left: " + Px(l) + "; top: " + Px(t) + "; width: " + Px(r - l) + "; height: " + Px(b - t) + ";\"></div>";
	}

	std::string HoverMarkup(Frame const& f, float const k, float const pw)
	{
		Hover const& h = f.hover;
		if (!h.valid) return {};
		float const x = h.x * k, y = h.y * k;
		// to the right of the mark, to its left near the edge
		std::string pos = x > pw - 260.f ? "right: " + Px(pw - x + 14.f) + ";" : "left: " + Px(x + 14.f) + ";";
		std::string out = "<div class=\"hov\" style=\"" + pos + " top: " + Px(std::max(0.f, y - 30.f)) + ";\">";
		if (h.kind != MarkKind::Item)
		{
			out += Escape(h.title);
		}
		else
		{
			out += "<span class=\"pile\">" + Escape(Str("tac.ovh.pile")) + "</span>";
			for (HoverRow const& r : h.rows)
			{
				out += "<span class=\"pile\">" + Escape(r.name);
				if (r.count > 1) out += "<span class=\"q\">x" + Num(r.count) + "</span>";
				out += "</span>";
			}
			if (h.hidden > 0) out += "<span class=\"more\">" + Escape(Fill(Str("tac.ovh.more"), Num(h.hidden))) + "</span>";
		}
		return out + "</div>";
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The view model

	struct LegendRowV
	{
		std::string cls, label;
		int n = 0;
		static void Describe(RowFields<LegendRowV>& f) { f("cls", &LegendRowV::cls)("label", &LegendRowV::label)("n", &LegendRowV::n); }
	};
	struct SquadRowV
	{
		int id = 0;
		bool sel = false;
		std::string name, status, face;
		static void Describe(RowFields<SquadRowV>& f)
		{
			f("id", &SquadRowV::id)("sel", &SquadRowV::sel)("name", &SquadRowV::name)("status", &SquadRowV::status)("face", &SquadRowV::face);
		}
	};
	struct RosterRowV
	{
		int idx = 0;
		bool sel = false;
		std::string name, face, label, cls;
		static void Describe(RowFields<RosterRowV>& f)
		{
			f("idx", &RosterRowV::idx)("sel", &RosterRowV::sel)("name", &RosterRowV::name)("face", &RosterRowV::face)
			 ("label", &RosterRowV::label)("cls", &RosterRowV::cls);
		}
	};
	struct KeyRowV
	{
		std::string key, text;
		static void Describe(RowFields<KeyRowV>& f) { f("key", &KeyRowV::key)("text", &KeyRowV::text); }
	};

	class OverheadViewModel final : public ViewModel
	{
	public:
		OverheadViewModel() : ViewModel("overhead", 0)
		{
			Command("close",  [](Args const&) { OverheadLeave(); });
			Command("select", [](Args const& a) { if (!a.empty()) NativePlacementSelect(std::atoi(a[0].c_str())); });
			Command("hover",  [](Args const& a) { if (!a.empty()) NativePlacementHover(std::atoi(a[0].c_str())); });
			Command("locate", [](Args const& a) { if (!a.empty()) OverheadLocateMerc(std::atoi(a[0].c_str())); });
			Command("clear",  [](Args const&) { NativePlacementClear(); });
			Command("spread", [](Args const&) { NativePlacementSpread(); });
			Command("group",  [](Args const&) { NativePlacementGroup(); });
			Command("done",   [](Args const&) { NativePlacementDone(); });
			Refresh();
		}

		void Describe(Fields& f) override
		{
			f.Field("title", title); f.Field("subtitle", subtitle); f.Field("head_icon", head_icon);
			f.Field("side_title", side_title); f.Field("side_meta", side_meta);
			f.Field("map_src", map_src);
			f.Field("placement", placement); f.Field("squad_empty", squad_empty);
			f.Field("pct", pct); f.Field("can_done", can_done); f.Field("mode", mode); f.Field("notice", notice);
			f.Field("l_legend", l_legend); f.Field("l_squad", l_squad); f.Field("l_empty", l_empty); f.Field("l_back", l_back);
			f.Field("l_clear", l_clear); f.Field("l_spread", l_spread); f.Field("l_group", l_group); f.Field("l_done", l_done);
			f.Field("help_clear", help_clear); f.Field("help_spread", help_spread); f.Field("help_group", help_group);
			f.Field("help_done", help_done);
			f.Rows("legend", legend); f.Rows("squad", squad); f.Rows("roster", roster); f.Rows("keys", keys);
		}

		void Refresh() override
		{
			Frame const& fr = CurrentOverheadFrame();
			placement = fr.placement;
			std::string const where = fr.sector + (fr.town.empty() ? "" : " " + fr.town);
			title = Str(placement ? "tac.ovh.title_place" : "tac.ovh.title") + " \xC2\xB7 " + where;
			subtitle = Str(placement ? "tac.ovh.sub_place" : "tac.ovh.sub");
			head_icon = placement ? "icon-destination" : "icon-map";
			l_legend = Str("tac.ovh.legend"); l_squad = Str("tac.ovh.squad"); l_empty = Str("tac.ovh.empty");
			l_back = Str("tac.ovh.back");

			legend.clear();
			Legend const lg = Count(fr.marks);
			auto add = [&](char const* cls, char const* key, int n) {
				if (n > 0 || std::string(cls) == "merc" || std::string(cls) == "item") legend.push_back({ cls, Str(key), n });
			};
			add("merc", "tac.ovh.k_mercs", lg.mercs);
			add("enemy", "tac.ovh.k_enemies", lg.enemies);
			add("militia", "tac.ovh.k_militia", lg.militia);
			add("civilian", "tac.ovh.k_civilians", lg.civilians);
			add("vehicle", "tac.ovh.k_vehicles", lg.vehicles);
			add("item", "tac.ovh.k_items", lg.items);

			squad.clear();
			for (SquadRow const& r : fr.squad) squad.push_back({ r.id, r.selected, r.name, r.status, FaceSrc(r.face, 56, 52) });
			squad_empty = squad.empty();

			roster.clear();
			can_done = false;
			mode = "clear";
			notice.clear();
			pct = 0;
			keys.clear();
			if (placement && NativePlacementActive())
			{
				Placement const& pl = NativePlacementState();
				mode = ModeName(pl.mode);
				can_done = pl.CanFinish();
				notice = NativePlacementNotice();
				pct = pl.Count() ? pl.PlacedCount() * 100 / pl.Count() : 0;
				side_title = Str("tac.ovh.arrival");
				side_meta = Fill(Str("tac.ovh.progress"), Num(pl.PlacedCount()), Num(pl.Count()));
				for (int i = 0; i < pl.Count(); ++i)
				{
					Piece const& p = pl.pieces[i];
					RosterRowV r;
					r.idx = i;
					r.sel = pl.mode == Mode::Group ? (pl.selected >= 0 && pl.pieces[pl.selected].group == p.group) : i == pl.selected;
					r.name = p.name;
					r.face = FaceSrc(p.face, 56, 52);
					bool const next = !p.placed && i == pl.selected;
					r.cls = p.placed ? "ok" : next ? "next" : "wait";
					r.label = Str(p.placed ? "tac.ovh.placed" : next ? "tac.ovh.next" : "tac.ovh.wait");
					roster.push_back(std::move(r));
				}
				keys.push_back({ "Click", Str("tac.ovh.hint_place") });
				keys.push_back({ "Right", Str("tac.ovh.hint_deselect") });
			}
			else
			{
				side_title = Str("tac.ovh.side");
				side_meta = where;
				keys.push_back({ "Click", Str("tac.ovh.hint_click") });
				keys.push_back({ "Hover", Str("tac.ovh.hint_hover") });
			}
			l_clear = gpStrategicString[STR_TP_CLEAR].to_std_string();
			l_spread = gpStrategicString[STR_TP_SPREAD].to_std_string();
			l_group = gpStrategicString[STR_TP_GROUP].to_std_string();
			l_done = gpStrategicString[STR_TP_DONE].to_std_string();
			help_clear = gpStrategicString[STR_TP_CLEARHELP].to_std_string();
			help_spread = gpStrategicString[STR_TP_SPREADHELP].to_std_string();
			help_group = gpStrategicString[STR_TP_GROUPHELP].to_std_string();
			help_done = gpStrategicString[can_done ? STR_TP_DONEHELP : STR_TP_DISABLED_DONEHELP].to_std_string();
		}

		std::string title, subtitle, head_icon, side_title, side_meta, map_src, mode, notice;
		std::string l_legend, l_squad, l_empty, l_back, l_clear, l_spread, l_group, l_done;
		std::string help_clear, help_spread, help_group, help_done;
		bool placement = false, squad_empty = true, can_done = false;
		int pct = 0;
		std::vector<LegendRowV> legend;
		std::vector<SquadRowV> squad;
		std::vector<RosterRowV> roster;
		std::vector<KeyRowV> keys;
	};

	// ---------------------------------------------------------------------------------------------------------------
	// The view

	struct View final : Rml::EventListener
	{
		std::unique_ptr<OverheadViewModel> vm;
		std::unique_ptr<Binding> binding;
		Rml::ElementDocument* doc = nullptr;
		bool active = false;
		std::string signature, layerMarkup, mapSrc;
		float scale = 1.f, picW = 0, picH = 0;
		int mapRev = -1, mapK = 0;

		// one queued click, run at the next update (not inside the event)
		bool pendingClick = false, pendingLeave = false, pendingDeselect = false;
		int clickX = 0, clickY = 0;

		void ProcessEvent(Rml::Event& ev) override
		{
			Rml::Element* const map = doc ? doc->GetElementById("ovh.map") : nullptr;
			if (!map) return;
			float const mx = ev.GetParameter<float>("mouse_x", -1.f), my = ev.GetParameter<float>("mouse_y", -1.f);
			Rml::Vector2f const o = map->GetAbsoluteOffset(Rml::BoxArea::Border);
			float const px = (mx - o.x) / scale, py = (my - o.y) / scale;
			bool const inside = px >= 0 && py >= 0 && px < MAP_W && py < MAP_H;
			std::string const type = ev.GetType();
			if (type == "mousemove")
			{
				if (inside) OverheadHover(int(px), int(py));
				else OverheadHover(-1, -1);
			}
			else if (type == "mouseout")
			{
				OverheadHover(-1, -1);
			}
			else if (type == "mouseup" && inside)
			{
				int const button = ev.GetParameter<int>("button", 0);
				if (button == 0)
				{
					pendingClick = true;
					clickX = int(px);
					clickY = int(py);
				}
				else if (button == 1)
				{
					if (!NativePlacementActive()) pendingLeave = true;
					else pendingDeselect = true;
				}
			}
		}
	};
	View g_view;

	void CloseView()
	{
		if (!g_view.active) return;
		if (g_view.doc) g_view.doc->Hide();
		g_view.active = false;
		g_view.signature.clear();
		g_view.layerMarkup.clear();
		Invalidate();
	}

	void OpenView()
	{
		RegisterSources();
		if (!g_view.doc)
		{
			g_view.vm = std::make_unique<OverheadViewModel>();
			g_view.vm->Update(true);
			g_view.binding = std::make_unique<Binding>(Context(), *g_view.vm);
			try
			{
				g_view.doc = LoadDocument("screens/overhead.rml");
			}
			catch (std::exception const& e)
			{
				SLOGE("native overhead: {}", e.what());
				g_view.binding.reset();
				g_view.vm.reset();
				return;
			}
			g_view.doc->AddEventListener("mousemove", &g_view, true);
			g_view.doc->AddEventListener("mouseup", &g_view, true);
			g_view.doc->AddEventListener("mouseout", &g_view, true);
		}
		if (!g_view.active)
		{
			g_view.doc->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
			g_view.active = true;
			g_view.signature.clear();
			g_view.layerMarkup.clear();
			g_view.mapSrc.clear();
			Invalidate();
		}
	}

	/** The picture's size in output pixels: the largest integer scale of the room the well leaves. */
	int FitToWell(Rml::ElementDocument* doc)
	{
		Rml::Element* const well = doc->GetElementById("ovh.well");
		if (!well) return 1;
		float const w = well->GetClientWidth(), h = well->GetClientHeight();
		return FitScale(w, h);
	}
}

bool TacticalOverheadActive() { return g_view.active; }

void TacticalOverheadUpdate()
{
	bool const want = OverheadIsNative() && NativeOverheadOpen() && TacticalHudActive();
	if (!want || !Start())
	{
		CloseView();
		return;
	}
	OpenView();
	if (!g_view.doc || !g_view.active) return;
	View& v = g_view;

	// what the view asked for since last frame
	if (v.pendingClick)
	{
		v.pendingClick = false;
		OverheadClick(v.clickX, v.clickY);
	}
	if (v.pendingLeave)
	{
		v.pendingLeave = false;
		OverheadLeave();
	}
	if (v.pendingDeselect)
	{
		v.pendingDeselect = false;
		NativePlacementDeselect();
	}
	if (!OverheadIsNative()) // the click left the overhead
	{
		CloseView();
		return;
	}

	Frame const& f = CurrentOverheadFrame();
	OverheadPicture const& pic = CurrentOverheadPicture();
	v.vm->Refresh();

	// the picture, at the largest integer scale that fits
	int const k = FitToWell(v.doc);
	if (Rml::Element* map = v.doc->GetElementById("ovh.map"))
	{
		float const pw = float(MAP_W * k), ph = float(MAP_H * k);
		if (v.picW != pw || v.picH != ph || v.scale != float(k))
		{
			v.scale = float(k);
			v.picW = pw;
			v.picH = ph;
			map->SetProperty(Rml::PropertyId::Width, Rml::Property(pw, Rml::Unit::PX));
			map->SetProperty(Rml::PropertyId::Height, Rml::Property(ph, Rml::Unit::PX));
			map->SetProperty(Rml::PropertyId::MarginLeft, Rml::Property(-pw / 2, Rml::Unit::PX));
			map->SetProperty(Rml::PropertyId::MarginTop, Rml::Property(-ph / 2, Rml::Unit::PX));
			Invalidate();
		}
	}
	std::string const src = "overhead-map@" + Num(k) + "~" + Num(pic.revision);
	if (src != v.mapSrc)
	{
		v.mapSrc = src;
		v.vm->map_src = src;
		v.signature.clear();
	}

	// markers over it
	std::string hot;
	if (f.hover.valid) hot = std::string(KindName(f.hover.kind)) + Num(f.hover.id);
	std::vector<Side> lit;
	if (f.placement && NativePlacementActive()) lit = NativePlacementState().LitSides();
	std::string const layer = ZoneMarkup(f, lit, float(k)) + ViewMarkup(f, float(k), v.picW, v.picH) + MarkMarkup(f, float(k), hot) + HoverMarkup(f, float(k), v.picW);
	if (layer != v.layerMarkup)
	{
		v.layerMarkup = layer;
		if (Rml::Element* e = v.doc->GetElementById("ovh.layer")) e->SetInnerRML(layer);
		Invalidate(2);
	}

	std::string sig = v.vm->Snapshot().ToJson();
	if (sig != v.signature)
	{
		v.signature = std::move(sig);
		v.vm->Changed();
		Invalidate(2);
	}
	v.doc->PullToFront();
}

namespace
{
	/** The minimap in the sector card: a left press (or a drag) centres the world view there, a right click opens
	 * the overhead, as the legacy radar did. */
	struct Mini final : Rml::EventListener
	{
		Rml::Element* radar = nullptr;
		float scale = 1.f, ox = 0, oy = 0;
		bool dragging = false;

		void ProcessEvent(Rml::Event& ev) override
		{
			if (!radar) return;
			std::string const type = ev.GetType();
			if (type == "mouseup" || type == "mouseout")
			{
				dragging = false;
				return;
			}
			Rml::Vector2f const o = radar->GetAbsoluteOffset(Rml::BoxArea::Content);
			float const px = (ev.GetParameter<float>("mouse_x", 0) - o.x - ox) / scale;
			float const py = (ev.GetParameter<float>("mouse_y", 0) - o.y - oy) / scale;
			if (type == "mousedown")
			{
				int const button = ev.GetParameter<int>("button", 0);
				if (button == 1)
				{
					if (!InOverheadMap()) GoIntoOverheadMap();
					return;
				}
				dragging = true;
			}
			if (dragging && px >= 0 && py >= 0 && px < MAP_W && py < MAP_H) OverheadCentreOn(int(px), int(py));
		}
	};
	Mini g_mini;
	Rml::Element* g_miniRadar = nullptr;
	std::string g_miniLayer, g_miniSrc;
}

void TacticalMinimapUpdate(Rml::ElementDocument* const hud)
{
	if (!hud) return;
	Rml::Element* const radar = hud->GetElementById("tac.radar");
	Rml::Element* const pic = hud->GetElementById("tac.radar.pic");
	Rml::Element* const layer = hud->GetElementById("tac.radar.layer");
	if (!radar || !pic || !layer) return;
	RegisterSources();
	if (g_miniRadar != radar)
	{
		g_miniRadar = radar;
		g_mini.radar = radar;
		radar->AddEventListener("mousedown", &g_mini, true);
		radar->AddEventListener("mousemove", &g_mini, true);
		radar->AddEventListener("mouseup", &g_mini, true);
		radar->AddEventListener("mouseout", &g_mini, true);
	}
	bool const ready = UpdateNativeMinimap();
	if (!ready)
	{
		if (!g_miniLayer.empty()) { layer->SetInnerRML(""); g_miniLayer.clear(); Invalidate(2); }
		return;
	}
	Frame const& f = CurrentOverheadFrame();
	OverheadPicture const& p = CurrentOverheadPicture();
	float const W = radar->GetClientWidth(), H = radar->GetClientHeight();
	if (W < 8 || H < 8) return;
	float const s = std::min(W / MAP_W, H / MAP_H);
	int const iw = std::max(1, int(std::floor(MAP_W * s))), ih = std::max(1, int(std::floor(MAP_H * s)));
	float const ox = std::floor((W - iw) / 2), oy = std::floor((H - ih) / 2);
	g_mini.scale = float(iw) / MAP_W;
	g_mini.ox = ox;
	g_mini.oy = oy;

	std::string const src = "overhead-mini@" + Num(iw) + "x" + Num(ih) + "~" + Num(p.revision);
	if (src != g_miniSrc)
	{
		g_miniSrc = src;
		pic->SetAttribute("src", src);
		pic->SetProperty(Rml::PropertyId::Left, Rml::Property(ox, Rml::Unit::PX));
		pic->SetProperty(Rml::PropertyId::Top, Rml::Property(oy, Rml::Unit::PX));
		pic->SetProperty(Rml::PropertyId::Width, Rml::Property(float(iw), Rml::Unit::PX));
		pic->SetProperty(Rml::PropertyId::Height, Rml::Property(float(ih), Rml::Unit::PX));
		Invalidate();
	}

	// the dots and the view box, in the minimap's own pixels
	float const k = g_mini.scale;
	std::string out;
	if (f.view.valid)
	{
		float const l = std::clamp(f.view.x * k, 0.f, float(iw)) + ox, t = std::clamp(f.view.y * k, 0.f, float(ih)) + oy;
		float const r = std::clamp((f.view.x + f.view.w) * k, 0.f, float(iw)) + ox, b = std::clamp((f.view.y + f.view.h) * k, 0.f, float(ih)) + oy;
		if (r - l >= 3 && b - t >= 3)
			out += "<div class=\"view\" style=\"left: " + Px(l) + "; top: " + Px(t) + "; width: " + Px(r - l) + "; height: " + Px(b - t) + ";\"></div>";
	}
	for (Mark const& m : f.marks)
	{
		if (m.kind == MarkKind::Item) continue;
		float const d = m.selected ? 7.f : 5.f;
		std::string cls = std::string("mk ") + KindName(m.kind);
		if (m.selected) cls += " sel";
		out += "<span class=\"" + cls + "\" style=\"left: " + Px(m.x * k + ox - d / 2) + "; top: " + Px(m.y * k + oy - d / 2) +
			"; width: " + Px(d) + "; height: " + Px(d) + "; border-width: 1px;\"></span>";
	}
	if (out != g_miniLayer)
	{
		g_miniLayer = out;
		layer->SetInnerRML(out);
		Invalidate(2);
	}
}

}
