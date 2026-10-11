# Tactical HUD — functional spec (M1)

| | |
|---|---|
| Legacy code | `src/game/Tactical/Interface.cc` (panels, movement/door menus, above-guy names and bars, top banners), `Interface_Panels.cc` (team panel and single-merc panel), `Interface_Items.cc` (inventory slots, item description box, money split, stack/key-ring popups, pick-up menu, item cursor), `Interface_Dialogue.cc` (NPC talk panel), `Dialogue_Control.cc` + `Faces.cc` (talking faces, subtitles), `Interface_Control.cc`, `Interface_Cursors.cc`, `Handle_UI.cc` (cursor modes), `Turn_Based_Input.cc` / `Real_Time_Input.cc` (keys and mouse), `TileEngine/Overhead_Map.cc`, `TileEngine/Radar_Screen.cc`, `TileEngine/Tactical_Placement_GUI.cc`, `Utils/Message.cc` (message lines) |
| Screen id | `GAME_SCREEN` (overhead map and placement are modes of it) |
| Reached from | map screen (Esc / sector), loading a save, sector entry |
| Native code | `src/game/NativeUI/TacticalHud.cc` (`TacticalViewModel`, "tactical"), `assets/ui/screens/tactical.rml`/`.rcss`; legacy hooks `Native*` in `Interface_Items.cc`, `Interface_Panels.cc`, `Message.cc`. Mocks: `assets/ui/mocks/phase5/*.rml` (generator `tools/ui/phase5_mocks.py`), shown over the real world by `src/game/NativeUI/MockWorld.cc` |
| ui_mode key | `tactical` (default native from 1280x720; legacy below) |
| Status | M3 (see "Implementation status"); wireframes approved |

## 1. How it was audited

- `tests/e2e/manual/phase5_audit.lua`: new campaign, Ivan, Barry, Grizzly and Buns hired **with equipment**, land in A9,
  squad spread out, then: team panel, merc hover, single-merc panel, item description (right-click on the hand slot),
  key ring, message line, overhead map, merc quote with subtitle, sector exit menu, game settings line (V), turn-based
  combat (D, real enemies in A9), placement. Each state dumps `ja2.ui{all}` + `ja2.texts()` + a PNG. It also saves
  `phase5_squad`, used by `tests/e2e/manual/phase5_mocks.lua`.
- Code read: the files above, especially `HandleModNone/Shift/Ctrl/Alt` (keys), `CreateSMPanelButtons`,
  `CreateTEAMPanelButtons`, `PopupMovementMenu`, `PopupDoorOpenMenu`, `InternalInitItemDescriptionBox`,
  `RenderItemDescriptionBox`, `RemoveMoney`, `InitializeItemPickupMenu`, `InitTalkingMenu`, `DrawSelectedUIAboveGuy`,
  `CreateTopMessage` / `InitPlayerUIBar` / `InitEnemyUIBar`, `Tactical_Placement_GUI` buttons and keys.
- Surprises: the single-merc panel *replaces* the team panel (you lose sight of the others); the action menu is a 3×3 icon
  grid whose middle-left button changes with the item in hand; prev/next merc and Map buttons are duplicated across the
  two panels; attachments can be removed only from the description box.

## 2. Information shown

| # | Information | Source | Legacy | Native (wireframe) |
|---|---|---|---|---|
| I1 | Squad members (face, name) | `gTeamPanel` slots, `GetPlayerFromInterfaceTeamSlot` | 6+ slots in the team panel (more at wide sizes) | squad bar cards (max 6), empty slots shown as ghosts |
| I2 | HP (with lost/bandaged part), energy, morale | `bLife/bLifeMax/bBleeding`, `bBreath`, `bMorale` | 3 vertical bars per face; tooltip with numbers | 3 labelled bars + numbers on each card; morale word in the detail panel |
| I3 | AP (combat) | `bActionPoints` | number on the face | AP badge on the portrait; dimmed card when 0 |
| I4 | Item in each hand + ammo | `inv[HANDPOS]`, `SECONDHANDPOS`, `ubGunShotsLeft` | two small pictures under the face | hand strip with the picture and `left/max`, amber when low |
| I5 | Status: bleeding, asleep, stealth, drunk, dying, mute, has new items, talking | `bBleeding`, `fMercAsleep`, `bStealthMode`, `GetDrunkLevel`, `bLife<OKLIFE`, `SOLDIER_MUTE`, face flags | face overlays / colours | status icons by the name (icon + tooltip, never colour alone); speaker icon when talking |
| I6 | Assignment / squad not current, stance | `bAssignment`, `usAnimState` | text above the guy | sub line on the card; above-guy label kept |
| I7 | Selected merc | `GetSelectedMan` | gold frame | amber card border + amber name in the world |
| I8 | Attributes (AGI DEX STR LDR WIS LVL MRK EXP MEC MED), armour %, weight %, camo % | profile / soldier | single-merc panel | detail panel: attributes as bars in physical / mental / skill groups; Load, Armour and Camo gauges (Load over 100% is hatched) |
| I9 | 22 inventory slots: 2 face, helmet, vest, legs, 2 hands, 3 worn LBE (vest, belt, pack), 12 typed pockets (4 per LBE item); status per item; stack counts; attachment marker | `inv[]` | single-merc panel | detail panel: a paper doll (`gen-doll` silhouette) with each worn slot where it is worn — face gear by the head, helmet, armour vest and LBE vest/pack at the chest, belt at the waist, hands at the sides, legs; pockets grouped by their carrier (vest pockets, belt pouches, pack); condition bar, count, attachment marker |
| I10 | Money carried, keys | `uiMoney` via money item, key ring | `$` button, key-ring button | Cash and Keys buttons with values |
| I11 | Item description: big picture, name, class, description, status, weight, damage, range, AP (single, burst, reload), ammo left and type, 4 attachments, pros/cons; money amount for money | `Item[]`, `Weapon[]`, `GenerateProsString` | box over the panel | side panel next to the detail panel (stats grid, ammo block with Unload, 4 attachment slots with "fits" list) |
| I12 | Above-guy: name, HP/energy bars, "(assignment)", "Squad n", reload/catch/pass prompts, GIVE, damage numbers | `DrawSelectedUIAboveGuy` | bitmap text over the merc | native labels anchored with `WorldToUi`; floating damage numbers |
| I13 | Message lines | `ScreenMsg` queue | ≤6 lines top-left, fading | same lines top-left (type marked by a left rule); full log on demand (new, see §8) |
| I14 | Turn / interrupt / enemy progress | `AddTopMessage`, `UpdateEnemyUIBar` | bar at the top with text | top-centre banner: Your turn (round, mercs with AP), Enemy/Militia/Creatures/Civilians turn with progress, Interrupt (amber) |
| I15 | Sector, time, radar | `Radar_Screen.cc`, clock | bottom-right radar + sector + time | top-right minimap card: sector, town, day/time, map with view box and dots, legend |
| I16 | Paused, time compression | `gfGamePaused` | "Game paused" box | toast/banner (unchanged behaviour) |
| I17 | Range to target, chance to hit, AP cost of the cursor action | `DisplayRangeToTarget`, cursor AP text | cursor text | target chip next to the cursor/target (hit %, AP) |
| I18 | Overhead map: whole sector, mercs, enemies seen, items, names on hover | `Overhead_Map.cc` | 640×320 picture | full-screen map (integer scale), legend with counts, squad list (new) |
| I19 | Placement: zone, placed/unplaced mercs | `Tactical_Placement_GUI.cc` | overhead + face list | overhead map + roster with placed/unplaced state and progress |

## 3. Actions

| # | Action | Legacy input | Game function |
|---|---|---|---|
| A1 | Select merc / select squad / next merc / next squad | click face, F1–F10 (Shift: no centring), Space, Shift+Space, 1–0 | `HandleSelectMercSlot`, `FindNextMercInTeamPanel`, `ChangeCurrentSquad`, `FindNextActiveSquad` |
| A2 | Open/close single-merc panel | double-click face, \` | `SetCurrentInterfacePanel`, `ToggleTacticalPanels` |
| A3 | Locate merc | `/`, click face twice | `LocateSoldier` |
| A4 | Stance up/down, stand, crouch, prone | PgUp/PgDn, S, C, P, buttons | `GotoHigherStance`, `HandleStanceChangeFromUIKeys` |
| A5 | Run / walk mode | R | `usUIMovementMode` |
| A6 | Stealth (merc / squad) | Z / Alt+Z, button | `HandleStealthChangeFromUIKeys` |
| A7 | Burst mode | B, button | `SetBurstMode` |
| A8 | Look / turn, talk, examine/hand cursor | L, T(button), Ctrl, buttons | cursor modes `LC_CHANGE_TO_LOOK`, talk, `HANDCURSOR` |
| A9 | Climb, cursor level (roof) | J, Shift+J, Tab, buttons | `ClimbUpOrDown`, `UIHandleChangeLevel` |
| A10 | End turn / enter turn-based | D, Done button | `I_ENDTURN`, `EnterCombatMode` |
| A11 | Map screen, options, save, load, quick save/load | M, O, Ctrl+S, Ctrl+L, Alt+S, Alt+L | `GoToMapScreenFromTactical`, `LeaveTacticalScreen` |
| A12 | Mute merc | button | `SOLDIER_MUTE` |
| A13 | Inventory drag: move, swap, stack, merge, give to merc, drop to ground, put in pocket by right-click | mouse | `HandleItemPointerClick`, `PlaceObject`, `AutoPlaceObject` |
| A14 | Item description: open, close, attach/detach (Ctrl+click), unload/reload ammo, cycle item | right-click, Esc | `InitItemDescriptionBox`, `DoAttachment`, `ItemDescAmmoCallback` |
| A15 | Money split | click money / `$` | `BtnMoneyButtonCallback*`, `RemoveMoney` |
| A16 | Stack popup, key ring popup | click stack, K | `InitItemStackPopup`, `InitKeyRingPopup` |
| A17 | Pick-up menu (All / OK / Cancel / scroll) | click items on ground | `InitializeItemPickupMenu` |
| A18 | Action (movement) menu: look, run, walk/drive, action (by item), cancel, sneak, talk, hand, crawl | right-click hold / U | `PopupMovementMenu` |
| A19 | Door menu: open/close, examine, boot, untrap, use key ring, crowbar, lockpick, explosive, cancel (AP/BP, disabled reasons) | click door | `PopupDoorOpenMenu`, `DoorAction` |
| A20 | Talk panel: Friendly, Direct, Threaten, Give (drop item on face), Recruit, Come again, Done; click name → profile | click NPC | `InitTalkingMenu`, `Converse` |
| A21 | Subtitle/face text continue | click face/text | `CONTINUE_OVER_FACE_STR` |
| A22 | Sector exit menu | click edge | `ja2.debug("exitmenu")` path |
| A23 | Misc keys: A auto-bandage, E cycle enemies, F range, G lights, H help, I item glow, N cycle stack, T roofs, V settings, W wireframe, X swap places, `=` select all, `*`/`,` red glow, Home 3D cursor, End continue move, F12 clear lines, Ctrl+Q swap hands, Ctrl+N head gear, Alt+R reload, Ctrl+Shift+R group reload, Insert overhead, keypad ± zoom | | see `Turn_Based_Input.cc` |
| A24 | Placement: click merc, click zone, Clear (C), Spread (S), Group (G), Done (Enter) | | `Tactical_Placement_GUI.cc` |
| A25 | Range and ballistics readout: compare two weapons over the damage pipeline | chart button in the detail panel | `NativeUI::OpenWeaponReadout` (see [weapon-readout.md](weapon-readout.md)) |

## 4. States

S1 realtime team view · S2 combat (our turn / enemy turn / interrupt) · S3 single-merc detail · S4 item description
(weapon, money, key) · S5 item in hand (drag) · S6 menus (action, door, pick-up, stack, key ring, exit) · S7 NPC talk
· S8 merc speaking (subtitle) · S9 overhead map · S10 placement · S11 paused · S12 meanwhile/cinematic (HUD hidden).

## 5. Popups

Action menu, door menu, pick-up menu, stack popup, key ring, money split, item description, talk panel, sector exit,
message boxes (native already), help screen (H, out of scope: Phase 7).

## 6. Cues

Face animations (eyes/mouth while talking), damage numbers, item glow, new-item flash on slots, top bar animation,
locator, cursor AP text, sounds on buttons and drops. All stay; face animation moves to the card portrait.

## 7. Edge cases

- Vehicles: the vehicle has a slot with passengers and HP as "condition"; driver/exit-vehicle tooltips
  (`DRIVER_POPUPTEXT`, `EXIT_VEHICLE_POPUPTEXT`) → card shows passengers; Drive in the action menu.
- EPCs (Mary, John, Skyrider…): no inventory change, can't be ordered to attack → card without AP actions; detail panel read-only.
- Robot: no stance/stealth, remote control item → tools disabled with reason.
- Dying (< OKLIFE) and bleeding: hatched critical bar + bleeding icon; dead: card greys, removed from squad at turn end.
- Asleep: asleep icon, portrait tinted.
- 18+ mercs: squads of 6; squad tabs 1–0 with member counts; Shift+Space; "All in sector" is not a squad (as legacy).
- Combat vs realtime: AP only in combat; End turn disabled (starts turn-based) in realtime; enemy turn locks input except Esc-to-skip anim.
- Mercs in another sector / in transit are not in the bar (as legacy).
- 640x480 and 32:9: the bar keeps cards ≤ 300 dp and uses extra width for more empty slots; at < 1280x720 the legacy HUD runs.

## 8. Deliberately dropped or changed (for owner approval)

| Item | Proposal | Reason |
|---|---|---|
| Team panel ↔ single-merc panel swap | The squad bar always stays; the detail panel opens *above* it | you keep seeing the squad; drag items onto other cards to give |
| Prev/next merc buttons | dropped as buttons; Space / ←→ in the detail panel | cards are clickable |
| Stance up/down arrows | three direct stance buttons (S/C/P) | one click instead of two |
| Mute button | moved to the detail panel header | rarely used |
| Radar in the bottom bar | top-right minimap card with sector, time and legend | bar height; GPU minimap |
| Action menu 3×3 icon grid | text menu with icons, keys and AP costs, grouped Move / Act | readable, shows keys |
| Door menu icons | menu with AP costs and the *reason* a choice is disabled | discoverability |
| Message log | the 6 fading lines stay; **new** full log panel with filters (key to confirm, proposed L — L is Look today) | history was only on the map screen |
| Item pictures | original art at integer scale (2× at 1080p, floor(2·dp) elsewhere), centred, never stretched | owner rule |
| Overhead map | integer-scaled, full screen, with legend and squad list | was 640×320 |

## Owner decisions (approved)

- All Phase 5 wireframes (the ten states in `assets/ui/mocks/phase5/`, screenshots in
  `pr-screenshots/native-phase-5-wireframes/`) and the changes in section 8 are approved by the owner.
- The message log key is **H**. H opens the tactical help screen today (`ShouldTheHelpScreenComeUp`), so help needs a new
  key at implementation time.
- Long guns must not be clipped in the hand slot. The hand slot and the card's hand strip must fit the widest small item
  picture at its integer scale, never cut it and never stretch it.

## Implementation status (Phase 5)

How it works: the native HUD is an overlay over `GAME_SCREEN`, not a screen. The legacy tactical screen keeps running
with its panels and regions under the native bar and draws nothing the HUD shows (names over mercs, message lines,
turn bar). Every native control acts through the legacy code, so the rules cannot drift:
- buttons press the legacy hotkey (`PressKey`: same handler, same checks);
- inventory slots, the description's attachments, Unload, Done and the money buttons go through the inventory core
  (`Equipment/InventoryCore.h`, issue #317): the core decides what a click means (take, put, pass between mercs with
  its AP, attach, merge as a question, stack popup, refusals with their reason), `InventorySlotClick`,
  `InventoryAttachClick`, `InventoryUnload` and `InventoryMoneyStep` apply the verdict. Nothing clicks a hidden legacy
  region. A merge, or an attachment that cannot come off again, is a question the HUD shows (`tac.ask`) and answers
  with `ask_yes` / `ask_no`; the held item is the core's hand (`InventoryHand()`), mirrored into `gpItemPointer`
  until #324;
- the item description shows `NativeItemDescData()`, the values the legacy box prints;
- the action, door and pick-up menus show the menus the legacy code built — same labels, AP costs and enable/disable
  rules (`NativeMovementMenuView`, `NativeDoorMenuView`, `NativeItemPickupView`) — and press their buttons
  (`NativeMenuClick`, `NativePickupClick`, `NativePickupOK`, …) while the legacy drawing of those menus is off.
The detail panel is open exactly when the legacy single-merc panel is (`, double click on a card).

The native UI draws in output pixels, and the window scales and letterboxes the game's canvas: everything anchored to
the world (names over the mercs, the action, door and pick-up menus) and the bar's height over the legacy panel go
through `CanvasToOutput` / `CanvasScale` (NativeUI.cc), never the UI scale. The native pointer is the cursor over the
HUD, and an item held by the mouse is a native picture on the pointer everywhere in tactical (`TacticalHudOwnsCursor`),
so it looks the same over the world and over the inventory.

### The cursor over the world (#318)

`Handle_UI.cc` still decides what the pointer means (a `UICursorID`, the action points, the texts the legacy cursor
wrote beside itself, the plotted path). The drawing is native:

| Part | Where | What |
|---|---|---|
| Core | `NativeUI/CursorModel.{h,cc}` | a compiled table maps every `UICursorID` to `{shape, tone, mode, marker, aim, why}`; `Evaluate` turns an `Input` (id, AP, texts) into the chip lines; `PlanPath` splits a path at the turn's budget. No globals; unit-tested (`CursorModel_unittest.cc`, including that every id is mapped). |
| Adapter | `Tactical/CursorAdapter.{h,cc}` | reads the legacy globals into an `Input`, plots the path (`PathAI.cc` records the tiles `PlotPath` plots, `PlottedTrail()`), and projects tiles with the soldier projection into world pixels |
| View | `NativeUI/TacticalCursor.cc` | `<cursorlayer>`: marker, path and destination as geometry (RmlUi transforms are not drawn by the software UI path); the chip beside the pointer; the pointer's shape (`SetCursorShape`) |
| Lua | `ja2.cursor()` | `{shown, mode, shape, tone, marker, ap, apLeft, hit, aim, why, target, tile, id, chip, lines, path = {steps, solid, total, now, next, beyond}, markerAt, destAt}` |

Tones: **ok** (can do), **warn** (with a catch: goes on next turn, out of range), **no** (cannot, and `why`), **foe** (the
click attacks). The marker diamond replaces the snapping tile cursor, the path is a line through the tile centres:
solid for this turn's action points, dashed amber beyond; the footstep tile nodes are not drawn while the native HUD is
up. The chance to hit is always worked out for the chip (`show_hit_chance` only gates the legacy cursor text).

### World overlays (#322)

`RenderTopmostTacticalInterface` used to draw the locators, the new-item markers, the burst impacts, the arrows, the
rubber band, the item list under the cursor, the clock and the pause box into the canvas. While the native HUD is up
none of that is drawn; the legacy timers and flags still decide *when* each exists.

| Part | Where | What |
|---|---|---|
| Core | `NativeUI/OverlayModel.{h,cc}` | ring phases, `ArrowsFor` (the `ARROWS_*` flags to chevrons), `NormalizeBand`/`BandGlow`, `BuildPool` (at most 8 rows and "+N more"), `PlaceList` (right of the anchor, else left, clamped). No globals; unit-tested (`OverlayModel_unittest.cc`). |
| Adapter | `Tactical/OverlayAdapter.{h,cc}` | reads the flash slots, the multi-purpose and merc locators (`UpdateMercLocator` is the timer, moved out of the legacy draw), the burst spread, the arrows, `gRubberBandRect`, the pool under the cursor, the pause flag and the civilian's quote (`CivQuoteBubble`) into a `Frame` |
| View | `NativeUI/TacticalOverlays.cc` | `<overlaylayer>`: rings, impact crosses, chevrons and the band as geometry; the item lists and the civilian bubble as HUD markup; the pause banner. World points go through `WorldToOutput`. |
| Lua | `ja2.overlays()` | `{paused, locators, bursts, arrows, band, pools, speech}`; scenario aids `ja2.debug("flashitem" / "arrows" / "burst" / "civquote")` |

The sector card is the clock and the town; the pause box is the banner at the top ("click or any key resumes"). A
civilian's line is a bubble over his head and ends on its own timer (the legacy box ended on a click on it too).

### Drag and drop (#320)

A press on a full pocket that moves more than `DRAG_THRESHOLD_DP` (6 dp) lifts the item into the hand (the same pick-up
as a click, so the pointer carries it); the button going up decides the rest. Click-to-pick and right-click stay.

| Let go over | What happens | Decided by |
|---|---|---|
| a pocket | put down, swap, attach or the merge question | `InventoryCore::PlanPlace` (`InventorySlotDrop` gives the verdict without the click) |
| a squad card | give: the card's merc must be within 3 tiles and in sight, each merc needs 3 AP and pays 2; on the card of the merc it came from, drop at his feet (`AP_PICKUP_ITEM`) | `Equipment/DragDrop` `PlanCardDrop` through `PlanDropOnCard` / `DropOnCard` (`Tactical/InventoryAdapter.cc`) |
| the world | drop, throw (arc, landing marker and chip of #318), give to a merc, drop at his feet when it is the merc himself | the cursor's legacy click handler (`HandleItemPointerClick`), whose give branch is now `PassHeldItemTo`, the same function the card uses |

A drag never leaves the item stuck: Esc or the right button while dragging, a release on a part of the HUD that
takes nothing, a refusal, or a release within 16 dp of a panel all send it back to its pocket at no cost. Only a release
clearly outside the HUD is the world's click. A click-to-pick item goes back with Esc, or the right button anywhere but on a pocket that holds something. The "Holding" chip shows over the HUD and within the same margin of it. A team
mate in reach under the pointer reads "Give" at 2 AP, not "Drop".

While something is held, every pocket of the detail panel and every card is tinted `dok` / `dno` (takes it / does not),
and over a pocket or a card the chip beside the pointer says what letting go does and, when it cannot, why
(`tac.drag.*` strings). Lua: `ja2.inventoryOp("plan_card", {to})`, `("drop_card", {to})`, `("plan_slot", {merc, slot})`;
`ja2.viewModel("tactical").cards[i].drop` and the slot rows' `drop` carry the outline.

| Done | Rows |
|---|---|
| Squad bar: cards (face, HP/lost, EN, MO, AP in combat, status icons, stance, hand item and ammo), squad tabs, select, details | I1–I7, A1–A2 |
| Tools: stance, run, stealth, burst, look, talk, climb, roof level; End turn / Turn-based, Map, inventory, log, options | A4–A11 |
| Detail panel: attributes, armour/weight/camo, vitals, all 22 slots including the worn LBE row and the 12 typed pockets (pick up / put down / description by click), cash, keys; mute (A12) and swap hands (A23) in the header, and the loadout screen button ([loadout.md](loadout.md)) | I8–I10, A12, A13, A16, A23 |
| Item description: picture, text, stats, status, ammo + Unload, 4 attachments, pros/cons, keys; money split +1000/+100/+10 (right click takes back) | I11, A14, A15 |
| Names, bars, assignment/catch/give prompts, health of others, damage numbers over the mercs | I12 |
| Message lines; message log on **H** with filters (help moved to **Shift+H**) | I13 |
| Turn / interrupt / enemy banner with progress | I14 |
| Sector, town, day and time; the minimap (the whole sector, the view box, a dot per merc / enemy seen / militia / civilian; click or drag centres the view, right click opens the overhead); overhead (Insert), tree tops, item glow | I15 |
| Overhead map (Insert, the sector card's button, right click on the minimap): the whole sector as one picture (the legacy small-tile traversal recorded through the world pipeline, 24-bit colour) at the largest integer scale that fits, a dot per merc / enemy seen / militia / civilian / vehicle and a square per pile, the view box, names on hover and what is in a pile, the legend with counts and the squad list; a click centres the world there and leaves, Esc / Insert / right click leave | I18 |
| Placement (a mode of the same view): the arrival strip of the selected merc lit (a card under the mouse shows its edge), the roster with placed / waiting / click the zone, progress, *Clear* (C), *Spread* (S), *Group* (G), *Done* (Enter, off until everyone is placed); a click in the strip puts the selected merc (or his group) at the nearest edgepoint, and says why when it cannot | I19, A24 |
| Action menu (right click held): Move and Act groups with keys and AP costs; door menu: the door's actions with AP costs and why a choice is off; pick-up menu: the items on the ground with All / Take / Cancel — all three are models (`PopupModels`), no legacy button or region is made | A17–A19 |
| Stack popup (right click on a stack): the objects with rounds or condition, click takes one into the hand or puts the hand's object in, *Take n* / *Take all*; key ring: each key with where it was found, *Use* on the door in front of the merc (with the reason when it is off), *Give* (the key in the hand) and *Put the key back*; a key in the hand dropped on a door uses it | A20 |
| Talk panel (the Phase 5 layout, centred over the bar): the NPC's face and line, the six approaches with keys 1-6, *Done* (Esc), the name asks who they are; the merc's own line as a subtitle over his head and the speaking face as a card | A20 |
| Sector exit menu: who leaves (selected merc / the squad), load the next sector now or travel on the map, what is off and why, the trip; Esc cancels, Enter goes | A21 |

Gaps, still legacy or not done in this PR:
- The popups need the native HUD (1280x720 and up, `ui_mode tactical=native`). Without it nothing can draw them, so
  each one takes its safe default: the action menu cancels, the door opens, the pick-up list takes everything, the
  sector exit goes as the dialogue opened, and a conversation says its first line and closes. The quest debug
  screen's talk panel no longer draws.
- The talk panel shows the NPC's face as a picture: no mouth or eyes animation yet (that is the squad card's work in
  #319). Recruit is never greyed out: whether she will join is the conversation's answer, not a rule the panel knows.
- The overhead map and the placement need the native HUD as well; with the legacy HUD (`ui_mode tactical=legacy`)
  they stay the legacy 640x320 views until #324 deletes them. The picture is the legacy small-tile art at
  1/5 scale: sharper than the legacy picture (24-bit colour, integer scale) but not HD art (#35).
- The overhead picture is redrawn about every 3 s while it is open and every 8 s for the minimap, not on every
  change; a door or a fire shows up with that delay.
- The detail panel's inventory stays click-to-pick, click-to-put; the dedicated loadout screen
  (`docs/ui/loadout.md`, the inventory button in the panel header) has real drag and drop, the LBE
  windows and the weapon platform. There is no "drop on a card to give" yet.
- The hit chance / AP chip at the target and the path cost of the wireframes are not done.
- Legacy/native equivalence is by construction (the native controls run the legacy handlers); there is no save-dump
  comparison test yet.

The menus follow the approved wireframes with two deliberate deviations: the item labels are the game's own strings
(`pTacticalPopupButtonStrings`, so they are localised and match the legacy tooltips — "Stand/Walk" instead of "Walk"),
and the pick-up buttons show no key hints (the legacy menu only listens to Esc; the wireframe's A/Enter keys would be
new bindings). The pick-up row hover still runs the legacy "compatible ammo" highlight.

## 9. Parity tour

`tests/e2e/tactical_parity.lua` (every resolution of `ctest -R resolution_`; goldens at 1280x720: `hud`, `detail`,
`desc`, `money`, `log`, `menu_action`, `menu_door`, `menu_pickup`):

| Row | Covered by |
|---|---|
| I1, I15 | the card is Ivan's with his gun and ammo; the sector card says A9 |
| A1, I7 | clicking the card selects him |
| A4 | crouch and stand buttons: `stance` changes |
| A6 | stealth toggles the merc's flag |
| A2, I8 | inventory button: the detail panel shows Ivan |
| A14, I11 | right click on the hand slot: description with 4 attachment slots; Unload empties the gun; clicking the gun with the magazine reloads it |
| A13 | an item moves from one small pocket to an empty one |
| A15 | cash: +100, Done, put in a pocket that takes it: the account loses 100 |
| I13 | H opens and closes the log |
| A18 | a right click held on the terrain: the action menu, titled with the merc; the Walk row closes it |
| A19 | `ja2.debug("doormenu")`: the door menu, titled Door; a disabled action says why; Examine runs the legacy action |
| A17 | `ja2.debug("pickupmenu")`: the pick-up menu lists the items; a row selects; Take 1 takes it |
| I14, A10 | turn-based: AP on the cards (when there is a fight) |
| layout | UI scale 150% and 200%: layout audit (`shots.take`) |
| < 1280x720 | the legacy HUD runs |
