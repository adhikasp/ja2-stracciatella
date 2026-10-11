# Native tactical — the native HUD as the only tactical UI

> **Status: proposal (issue #316), for owner approval.** It gates #317–#324. Wireframes for the states that
> change are listed in [Wireframes](#wireframes); they are approved per state, as for every native screen.

## Goal

Tactical has one UI: native C++ cores with no globals, thin adapters onto the soldier and item globals, and the
native HUD drawn at output resolution. The legacy team and single-merc panels, their mouse regions, buttons, cursors
and popups are deleted, not hidden. There is no `ui_mode tactical=legacy` and no fallback below 1280x720.

## Where things stand (after #315)

The native HUD (`src/game/NativeUI/TacticalHud.cc`) is a skin over the legacy one:

| Coupling | Where | Consequence |
|---|---|---|
| The team / single-merc panels run hidden under the bar | `Interface_Panels.cc`, `SetCurrentInterfacePanel` | two UIs alive, the world viewport ends at the legacy panel's top, `TEAMPANEL_HEIGHT` sizes the native bar |
| Buttons press legacy hotkeys | `PressKey` in `TacticalHud.cc` | a button can only do what a key does; no availability or "why not" |
| Inventory, description, money and key ring click hidden legacy regions | ~~`NativeInvSlotClick`, `NativeItemDescAttachmentClick`, `NativeItemDescUnload`, `NativeMoneyButton`, `NativeSMMoneyClick`, `NativeKeyRingClick`~~ gone with #317 (they call the inventory core); `NativeSMMuteClick` stays until #319 | the rules live in `Equipment/InventoryCore` |
| Action, door and pick-up menus are legacy button panels shown natively | ~~`NativeMovementMenuView`, `NativeDoorMenuView`, `NativeItemPickupView`, `NativeMenuClick`, `NativePickup*`~~ gone with #321: they are `PopupModels` read through `Tactical/PopupAdapter.h`, no button or region is made | the rules live in `NativeUI/PopupModels` |
| Detail panel open == legacy SM panel open | `gsCurInterfacePanel == SM_PANEL` | the HUD's state is a legacy global |
| Stack and key-ring popups hide the HUD | ~~`Wanted()`~~ gone with #321: the HUD is never hidden, the popups are native | the legacy panel shows through |
| Two coordinate systems | canvas (`SCREEN_WIDTH x SCREEN_HEIGHT`, mouse, world picking) and output (native UI) | `CanvasToOutput` everywhere; legacy pixels at canvas resolution |
| Two cursors | `Interface_Cursors.cc` / `Cursor_Control` bitmaps vs the native pointer | `TacticalHudOwnsCursor` switches between them |
| Canvas-pixel world overlays | `RenderTopmostTacticalInterface` (`Interface_Control.cc`): locators, item pool list, flashing items, burst marks, arrows, rubber band, pause box, clock; `Civ_Quotes.cc` bubbles | low-resolution UI over a high-resolution world |

Rules hiding in the panel code, which must move into cores and not be lost: AP costs of moving an item between two
mercs (3 AP check, 2 AP each), `HandleNailsVestFetish`, the merge confirmation (`ValidMerge` + message box),
attaching by dropping on a worn/held item (`ValidAttachment`), `CleanUpStack` on Ctrl+click, the stack popup on a
click on a stack, `HandleTacticalEffectsOfEquipmentChange`, the "item passed to merc" message, the hatching of
items that cannot go somewhere (`ReevaluateItemHatches`), give by dropping on a face, camo by dropping on the
portrait, the enemy-seen indicator on a face, stat-change highlights, and the shopkeeper special cases (which stay
with the shopkeeper screen).

## Target architecture

```
 SDL input ──► TacticalInput (one router per frame, output pixels)
                 1. a modal native layer (menu, popup, drag in progress) takes everything
                 2. else the HUD, if the pointer is over an element with class "hit"
                 3. else the world: WorldPointer (move, buttons, wheel) ──► Handle_UI state machine
 keys ─────────► native focus (text fields) ─► else TacticalCommands key map

 cores (no globals, gtest)            adapters (the only code that touches globals)       views (RmlUi, output pixels)
 ─────────────────────────            ──────────────────────────────────────────────      ────────────────────────────
 InventoryCore     (#317)   ◄──────►  InventoryAdapter: SOLDIERTYPE::inv, item pools,  ──► detail panel, item sheet,
 TacticalCommands  (#319)             gTacticalStatus, AP, quotes, Observables              loadout, drag & drop (#320)
 CursorModel       (#318)   ◄──────►  CursorAdapter: Handle_UI mode + UICursorID,      ──► native pointer, target chip,
                                      CTH, AP, path                                         path line, ground marker
 SquadRoster       (#319)   ◄──────►  roster order, squads, faces' frame state         ──► squad bar cards
 PopupModels       (#321)   ◄──────►  stack, key ring, sector exit, talk, action/door/ ──► native popups
                                      pick-up menus
 OverlayModel      (#322)   ◄──────►  locators, item pools, burst marks, rubber band   ──► world overlay layer
 OverheadModel     (#323)   ◄──────►  small-tile map, placement zones and pieces       ──► overhead view, placement,
                                                                                            sector-card minimap
```

Every core follows the house shape (AGENTS.md): a core with no globals covered by gtest, a thin adapter, an
`Observable` at each decision point, and a Lua surface so `ja2ctl state` / `eval` read it as data and
`BattleScenario` / `CampaignScenario` set it up. The existing `Equipment/` modules (`PocketRules`,
`AttachmentRules`, `LoadoutModel`, `Stash` + `Strategic/SectorStock`) are the model.

### Layers and coordinates

- **World layer:** the GPU world renderer fills the whole output. The HUD is drawn over it; the camera centre
  ("locate merc", sector entry) and the visible-area tests subtract the bar's real height in output pixels.
- **UI canvas:** `GAME_SCREEN` draws nothing into `FRAME_BUFFER` any more. `SCREEN_WIDTH x SCREEN_HEIGHT` stays only
  as the legacy screens' surface until Phase 10 (#36).
- **Native layer:** HUD, popups, cursor, world overlays, overhead map. Anything anchored to the world is placed by
  projecting a world point (tile centre, soldier head) straight to output pixels with the world camera
  (`WorldToOutput`), not through the canvas. `CanvasToOutput` disappears from tactical.
- **Picking** takes output pixels: `OutputToWorld` (the exact inverse of the camera, integer math as
  `UiToWorldQ`), so a click at 4K picks the same tile as the software path at 1x (the Phase 8 picking test extends
  to it).
- **Art:** item and face pictures at integer scale (`floor(base * dp)`), never stretched, until HD art (#35).
  Everything else (icons, frames, text, markers, lines) is vector/procedural at output resolution.

### Input: who owns the mouse

No legacy mouse regions in `GAME_SCREEN`. `gViewportRegion` and every panel/popup region are replaced by
`TacticalInput`:

1. A **modal native layer** (an open menu or popup, a drag in progress, a message box) gets every event; a click
   outside closes it as Esc does.
2. Otherwise the **HUD** gets the event if the hover element (RmlUi hit test) is inside a `hit` element.
3. Otherwise the **world** gets it: `WorldPointer` holds the output position, the world point under it, button
   state and wheel. `Handle_UI.cc` and `Turn_Based_Input.cc` / `Real_Time_Input.cc` read `WorldPointer` instead of
   `gViewportRegion.uiFlags` / `ButtonState`; the state machine itself (what a click on the world means) stays.
4. **Edge scrolling** uses a 2-pixel strip on the outer output edges, which the HUD never blocks.

Keys: a focused native text field first; otherwise `TacticalCommands` maps the key to a command. The native
buttons call the same commands. `PressKey` is deleted.

### TacticalCommands (#319)

A compiled table: command id (`stance.crouch`, `end_turn`, `climb`, `detail.toggle`, …), label key, icon, default
key, and an availability function returning `{enabled, why}`. The adapter executes each command through the
legacy action function (`HandleStanceChangeFromUIKeys`, `UIHandleEndTurn`, `ClimbUpOrDown`, …), never through a key
event. gtest: no two commands share a key, every command has a label and an icon, every HUD button names a command.
Lua: `ja2.tactical.commands()` lists them with availability; `ja2.tactical.run("stance.crouch")`. The HUD shows
`why` in the tooltip of a disabled button.

### InventoryCore (#317)

Owns the **hand** (the held stack, replacing `gpItemPointer`) and the operations: pick up, put down, swap,
stack/merge (with the confirmation as a returned question, not a message box), split, attach/detach, unload/reload,
split money, give to another merc (range and AP rules), drop to the ground, throw hand-off to the cursor,
auto-place on right click. Input is a plain loadout (22 slots, pocket types from `PocketRules`, LBE from `Lbe`) plus
a context (combat, AP left, consciousness, distance); output is `{ok, why, apCost, effects}`. The adapter applies
the result to `SOLDIERTYPE::inv`, the item pools and AP, plays the quote/sound, and raises Observables
(`BeforeInventoryMove`, `OnInventoryMoved`). During the transition the adapter mirrors the hand into
`gpItemPointer` so the remaining legacy consumers keep working; #324 removes the mirror. The tactical detail panel,
the item sheet, the money split, the loadout screen and the native map-screen gear panel all call the core.

### CursorModel (#318)

`Handle_UI.cc` keeps deciding *what* the pointer means (`guiNewUICursor`, the mode, the target). The drawing goes:
a table maps every `UICursorID` to a native cursor spec `{shape, tone (ok/warn/no), chip lines}` — gtest checks that
every id is mapped. The target chip shows hit chance, AP cost, aim level and burst/auto; the move cursor shows the
path as a native line through the tile centres (AP-affordable part solid, the rest amber) with the AP cost at the
destination, and a native ground marker replaces the snapping tile cursor and the footstep tile nodes. The
hit-location and interactive-tile texts (`SetHitLocationText`, `SetIntTileLocationText`) become chip lines. Lua reads
`{mode, shape, ap, hit, why}`.

## What is deleted

| File | Deleted | Kept (moved) |
|---|---|---|
| `Tactical/Interface_Panels.cc` | all of it: team and SM panels, their regions and buttons, rendering, filler, panel switching | rules listed above → InventoryCore; roster order (`gTeamPanel`) → SquadRoster; enemy indicator, stat-change highlight → card / detail view models |
| `Tactical/Interface_Items.cc` | tactical use of slot regions, item description box, money split, stack and key-ring popups, pick-up menu, item cursor, every `Native*` shim | pick-up rules → PopupModels; the box and slot drawing stay only while the legacy map screen and shopkeeper still call them (removed with them in #36) |
| `Tactical/Interface.cc` | panel switching, button panels, movement and door menus as buttons, above-guy drawing, top-message drawing | menu contents (items, AP, enabled/why) → PopupModels; top-message timing stays as game flow |
| `Tactical/Interface_Cursors.cc` | all drawing, the snapping cursor | the `UICursorID` set, as the CursorModel's input |
| `Tactical/Handle_UI.cc` | `DrawUICursor`/`HideUICursor` calls, `gViewportRegion` setup, footstep path nodes | the UI state machine |
| `Tactical/Interface_Control.cc` | the canvas draws of `RenderTopmostTacticalInterface` | which overlays exist and when → OverlayModel |
| `Tactical/Faces.cc` | blitting faces into panels | blink/mouth timing → the frame state the card shows |
| `Tactical/Interface_Dialogue.cc` | the talk panel UI and regions | NPC conversation logic (`Converse`, NPC actions) |
| `TileEngine/Overhead_Map.cc`, `Tactical_Placement_GUI.cc`, `Radar_Screen.cc` (tactical) | UI, buttons, regions, 640x320 rendering | small-tile traversal (recorded, below), placement rules (zones, spread, group) |

## Rendering of the overhead map (#323)

The overhead traversal (the small-tile set the original made for it) is recorded through the Phase 8 path
(`WorldPipe`) for the **whole sector** into one texture, shown full screen at the largest integer scale that
fits; mercs, seen enemies, items and names are native markers. The sector card's minimap is the same texture with
the view box. Placement is a mode of the same view with the roster and Clear / Spread / Group / Done.

## Minimum output size

**Proposed: 1280x720 output is the minimum for the game.** The native runtime is already unavailable below it, so a
smaller output means legacy everywhere. Below 1280x720 the video settings offer nothing smaller and a configured
smaller resolution starts at 1280x720. The HUD's layout scales by dp down to that size (compact rules, as the bar
already drops cards that do not fit). The 640x480 tactical goldens and `< 1280` test branches are deleted in #324.
*(Owner decision — alternative: allow dp < 1 down to 1024x576 with a compact layout.)*

## Slices, order and how each stays shippable

| # | Slice | Needs | Shippable because |
|---|---|---|---|
| #317 | Inventory core + adapter; detail panel, item sheet, money and loadout call it; `Native*Click` shims gone | #316 | the legacy panels still exist but nothing native clicks them; `tactical_parity.lua` passes through the core |
| #318 | Native cursor for every mode, target chip, path line | #316 | legacy cursor drawing switched off on `GAME_SCREEN` only; state machine untouched |
| #322 | Native world overlays. **Landed:** `NativeUI/OverlayModel`, `Tactical/OverlayAdapter`, `NativeUI/TacticalOverlays`, `ja2.overlays()`, `tests/e2e/tactical_overlays.lua` | #316 | each overlay switches individually; legacy draw deleted per overlay |
| #323 | Native overhead map, placement, minimap. **Landed:** `NativeUI/OverheadModel`, `Tactical/OverheadAdapter`, `NativeUI/TacticalOverhead`, `ja2.overhead()` / `ja2.overheadOp()`, `tests/e2e/tactical_overhead.lua` | #316 | Insert and placement open the native views; each legacy draw stays only for the legacy HUD and goes with #324 |
| #321 | Native popups: stack, key ring, talk (with external speaking faces and subtitles), sector exit — **plus the action, door and pick-up menus as models instead of hidden legacy buttons** (scope added here). **Landed:** `NativeUI/PopupModels`, `Tactical/PopupAdapter`, `ja2.popup()` / `ja2.popupOp()`, `tests/e2e/tactical_popups.lua` | #317 (stack, key ring) | each popup replaces its legacy one; `Wanted()` never hides the HUD again |
| #319 | No legacy panels: TacticalCommands, `TacticalInput`/`WorldPointer`, SquadRoster, face animation on cards, world fills the output, native message boxes in tactical | #317, #321 (stack/key ring) | the battle e2e tests and the 1080p suite pass with no panel created |
| #320 | Drag and drop everywhere; give by dropping on a card; drop/throw on the world | #317, #318 | click-to-pick stays; drag is the same core operation |
| #324 | Retire: no `tactical` mode key, no `gpItemPointer` mirror, dead code and legacy goldens deleted, `docs/ui/tactical.md` native only | all above | — |

#317, #318, #322 and #323 can run in parallel. Each slice has its parity rows in `tactical_parity.lua` and
asserts results through the Lua surface, never a click path or a screenshot.

## Wireframes

For owner approval, one state at a time. The mocks (`assets/ui/mocks/tactical/*.rml`, written by
`tools/ui/native_tactical_mocks.py`, style `mocks.rcss`) do not redraw the HUD: each one is opened over the **live
Field Kit HUD** in the real game, with the real squad, and adds only what changes. Shots are taken by
`tests/e2e/manual/native_tactical_mocks.lua`; the images are on `pr-screenshots/plan-native-tactical/`. Data in a
mock is literal (sample keys, sample hit chance).

| # | State | Slice | What it shows |
|---|---|---|---|
| W1 | `drag_slot` | #320 | a first aid kit dragged over the helmet slot: free pockets and hands outlined as valid, worn-gear and LBE slots refuse, the chip says why |
| W2 | `drag_give` | #320 | the same item over Barry's card: "Give to Barry · 1 tile · 2 AP each"; Buns is out of reach and says so |
| W3 | `drag_world` | #320 | a grenade held over the world: throw arc, landing marker, range, AP and spread; a click on the merc drops it at his feet |
| W4 | `cursors` | #318 | the cursor sheet: every mode as one native pointer with a tone (can do / with a catch / hostile or can't), the legacy ids each replaces, and the three chip kinds |
| W5 | `target` | #318 | the target cursor on a real enemy in turn-based combat: hit chance, aim clicks, AP, range |
| W6 | `move` | #318 | the move path as a native line through the tile centres: solid for this turn's AP, amber beyond, the cost at the destination |
| W7 | `stack` | #321 | the stack popup over the pocket it came from: each magazine with its rounds, take one / n / all |
| W8 | `keyring` | #321 | the key ring: each key with where it fits and where it came from, Use (with why it is off) and Give |
| W9 | `talk` | #321 | the approved Phase 5 talk panel in the Field Kit style: the NPC's talking face, the line, the approaches with keys, the merc's subtitle over his head |
| W10 | `exit` | #321 | the sector exit menu next to the edge: who leaves, load the next sector now or travel on the map, travel time and what is known there |
| W11 | `overhead` | #323 | the overhead view: the whole sector (the legacy 640x320 picture stands in for the GPU one), markers, the view box, legend with counts, the squad |
| W12 | `placement` | #323 | placement: the arrival zone lit, the roster with placed / waiting, progress, Clear / Spread / Group / Done |
| W13 | `overlays` | #322 | world overlays: locator rings, rubber-band selection, the item list under the cursor, a new-item marker, burst impacts, the paused banner |
| — | `live` | — | the live HUD as it is, at 1920x1080 and at 1280x720 (the proposed minimum) |

HUD-relative parts are placed in dp measured on the live HUD at 1920x1080, so W1–W3, W7, W8 and the world-anchored
states are shot at 1920x1080; the self-contained ones (W4, W9, W10) also at 1280x720.

Notes from drawing them: the ground marker is a rounded tile marker, not the isometric diamond (the software UI
renderer does not draw RmlUi transforms; the real marker is drawn in the world's projection by the overlay layer);
at 1280x720 today's bar is taller than its layout because it still reaches up to the hidden legacy panel — that
goes away with #319.

## Open questions for the owner

1. Minimum output 1280x720 for the whole game (above), or a compact layout below it?
2. The path as a native line instead of the original footstep tiles (W6) — OK?
3. The action, door and pick-up menus become models in #321 (not a separate issue) — OK?
4. Drag a key onto a door to use it (W8), and drop on the merc himself to drop at his feet (W3) — OK as new gestures?
