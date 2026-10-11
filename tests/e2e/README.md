# End-to-end tests

Each `*.lua` file here is a script that drives the real game headless, through
its real UI, and checks what happens. See [docs/automation.md](../../docs/automation.md)
for the full API.

| Script | Covers |
|---|---|
| `main_menu.lua` | Menu entries, Preferences and Credits and back |
| `laptop_tour.lua` | New campaign; every laptop program, reading mail, A.I.M. |
| `new_game_to_tactical.lua` | Hire a merc on A.I.M., land in Omerta, save/load round trip |
| `tactical_move_merc.lua` | Select a merc and walk to a clicked tile and back |
| `battle_smoke.lua` | A staged fight (3 decked-out mercs vs 10 enemies): fire, damage, AP, the native tactical HUD and a win (runs at 1920x1080, see [docs/plan/e2e-tactical-battles.md](../../docs/plan/e2e-tactical-battles.md)) |
| `battle_los.lua` | A pin-pointed, staggered enemy line: line of sight, cover, positioning, AP, morale and casualties (#64) |
| `battle_militia.lua` | An AI battle: 20 player militia vs 10 low-level enemy soldiers on a rural map, both sides spread over predetermined tiles, one observer merc far away; asserts the AI fires, takes cover, manoeuvres and breaks morale, and that the militia won (runs at 1920x1080, #65) |
| `battle_ai_eval.lua` | The AI evaluation harness: one militia-vs-enemies matchup from `-arg`, played out AI-only, read back through `ja2.battleReport()` (outcome, losses, time to disengage, objective) and printed as an `AIEVAL` line for the matrix runner `tools/ai_eval.py` (runs at 1920x1080, see [docs/plan/ai-evaluation.md](../../docs/plan/ai-evaluation.md), #59) |
| `loadout_spec.lua` | A loadout builds through the equipment rules, and the rules refuse a mount mismatch, an LBE item in a pocket and a medkit in a magazine pocket (#96) |
| `loadout_matrix.lua` | The damage pipeline as a table: ammo type x armour tier x range through the range lane, plus noise, weapon and armour wear, and reproducibility (issue [#263](https://github.com/adhikasp/ja2-pecel-bakwan/issues/263)) |
| `battle_loadout_arc.lua` | A whole loadout - weapon, attachments, ammo, LBE, pockets, armour - taken into a firefight and read back afterwards (runs at 1920x1080, #263) |
| `campaign_state.lua` | Author a campaign state (day, money, towns, roster, quests) on the live globals, assert it and the status model, then save/load it (see [docs/plan/e2e-campaign-state.md](../../docs/plan/e2e-campaign-state.md)) |
| `battle_campaign.lua` | Walk into a controlled town with townsfolk, then step into a tactical battle there (runs at 1920x1080) |
| `world_map_steps.lua` | The world-map steps: plot a path, move a squad between sectors and arrive, trigger an encounter and retreat from it, and board, fly and leave the helicopter (#69) |
| `world_map_actions.lua` | The strategic actions: split a squad, rest, doctor/patient, repair, train, and move an item between two mercs through the sector inventory (#69) |
| `map_screen_tour.lua` | Map screen: pause, inventory, options, laptop |
| `video_switch.lua` | Runtime video changes (`ja2.setVideo`) through four modes on tactical, map and laptop |
| `video_options.lua` | The Video options screen: change and apply resolution without a restart |
| `credits_parity.lua` | Native credits parity tour by element id (docs/ui/credits.md); legacy fallback below 1280x720 |
| `inventory_core.lua` | The inventory core as data (#317): take, put, refusals, a pass between two mercs with its AP, the merge question and its answer, the item sheet's unload, all through `ja2.inventory()` / `ja2.inventoryOp()` |
| `battle_cursor.lua` | The native tactical cursor as data (#318, runs at 1920x1080): over the HUD there is no world cursor, a move with its path and AP, a move that spills into the next turn, an enemy under the pointer with hit chance and AP, a held item over the world, all through `ja2.cursor()` |
| `tactical_overhead.lua` | The overhead map, the placement and the sector card's minimap as data (#323): the minimap's marks and view box, the overhead's legend, squad, hover (a name, what is in a pile), the click that centres the view, Esc / Insert / the button, and the placement's strip, refusals, Clear / Spread / Group / Done, all through `ja2.overhead()` / `ja2.overheadOp()` |
| `tactical_overlays.lua` | The tactical world overlays as data (#322, runs at 1920x1080): the merc locator, the pause banner, the rubber band, the item list under the cursor and beside a spotted item, the up/down arrows, burst impacts and a civilian's bubble, all through `ja2.overlays()` |
| `tactical_popups.lua` | The tactical popups as data (#321, runs at 1920x1080): the action, door and pick-up menus, the stack popup (take one, put back, take n), the key ring (use with the reason it is off, take, put back, a key dropped on a door), the talk panel (keys 1-6, nothing chosen while they speak) and the sector exit menu, all through `ja2.popup()` / `ja2.popupOp()` |
| `battle_drag.lua` | Drag and drop in the tactical HUD (#320, runs at 1920x1080): a click still picks, a drag between pockets moves the item, a drag onto a squad card gives it (2 AP each) or on his own card drops it at his feet, the hover verdicts through `ja2.inventoryOp("plan_card" / "plan_slot")` |
| `tactical_parity.lua` | Native tactical HUD parity tour by element id (docs/ui/tactical.md): squad bar, inventory, item description, money, log, the action, door and pick-up menus |
| `native_ui.lua` | Native UI runtime on legacy screens: native message box (mouse, focus, shortcuts), toasts, UI scales, a view model |
| `legacy_script.txt` | The old line-based `-uitest` format still works |
| `check_determinism.py` | Same script and seed, twice: identical screenshots |
| `check_sessions.py` | Concurrent `ja2ctl` sessions: worktree-scoped roots, names, logs and free ports |

`lib/campaign.lua` has the shared steps (new game, hire from A.I.M., land,
dismiss popups, and the campaign-state helpers `stage`, `at`, `assertState`,
`enterSector`, `enterTown`, `stepIntoBattle`). `lib/worldmap.lua` has the
world-map steps (`staged`, `plot`, `travel`, `waitLanded`, `assign`, `resume`,
`runHours`) that the two `world_map_*.lua` tests drive. `lib/battle.lua` has the
battle fixtures and orders (stage a fight, select a merc, shoot, end the turn,
read the militia back) for the tactical battle e2e track. `lib/loadout.lua` has
the equipment fixtures - see below.

The campaign steps reseed the RNG at their flow points (`campaign.reseed`: a new
game, entering the map; `BattleScenario` reseeds when it stages a battle), so a
UI change cannot churn the goldens of screens further down the flow by drawing a
different number of incidental random numbers (issue #177).

## Equipment fixtures

The equipment revamp ([docs/plan/equipment-revamp.md](../../docs/plan/equipment-revamp.md))
needs every equipment issue to assert against the same thing, so it is built once
here rather than per issue. `lib/loadout.lua` is the front door; `loadout_matrix.lua`
and `battle_loadout_arc.lua` are the tests that use it.

A **gear table** is what a fixture writes once and every path builds from. Every key
goes through the real equipment rules, so a refusal (an attachment that does not mount,
an item that does not fit its pocket, a magazine of the wrong calibre) fails the
fixture with the reason instead of quietly building something else:

```lua
loadout.rifle{
	weapon      = "MINI14",       -- the platform in the hand
	ammo        = "AMMO_AP",      -- the magazine in it, by ammo type or by magazine name
	condition   = 100,            -- the weapon's condition, 1..100
	armour      = "kevlar",       -- "none" | "kevlar" | "spectra"
	attachments = { optic = "SNIPERSCOPE", muzzle = "SILENCER" },
	lbe         = { vest = "LBE_VEST", belt = "LBE_BELT", pack = "LBE_PACK" },
	pockets     = { POCK1 = "FIRSTAIDKIT", POCK10 = { item = "CANTEEN", count = 2 } },
	stats       = { marksmanship = 100, health = 100 },
}
```

- `loadout.read(name)` - the gear a merc is carrying, as `ja2.loadout()` reads it back,
  with the armour flattened to plain names.
- `loadout.stage(spec)` - a fight, with the gear on our mercs. `spec.gear` is the gear,
  `spec.our` the per-merc overrides.
- `loadout.lane(spec)` - a **range lane**: one shooter, one target at an exact distance,
  a fixed seed, and `spec.shots` shots down it. Every shot is fired against a target
  restored to full health and full-condition armour, so a run of N shots is N
  measurements rather than a running total; the weapon keeps its condition, because its
  wear is one of the things measured.
- `loadout.matrix(spec)` - ammo type x armour tier x range, one lane per cell.

A lane hands back what the pipeline decided for each shot, as data:

```lua
local lane = loadout.lane{ distance = 8, shots = 6, ammo = "AMMO_AP", armour = "spectra" }
lane.shots[1].chanceToHit      -- the chance the roll was taken against
lane.shots[1].roll             -- the roll itself
lane.shots[1].hit              -- the roll connected
lane.shots[1].impacted         -- a round arrived at all (separate decision from the roll)
lane.shots[1].impactBeforeArmour -- the damage the round wanted to do
lane.shots[1].armourProtection -- what the armour in the way absorbed
lane.shots[1].damage           -- what got through
lane.shots[1].penetrated       -- armour was in the way and the round got through anyway
lane.shots[1].hitLocation      -- where it landed
lane.shots[1].noiseVolume      -- what the shot's noise was
lane.shots[1].wear             -- what it did to the weapon's condition
```

Plus `lane.summary` (shots, hits, impacts, damage, protection, penetrated, noise) and
`lane.targetArmour` (what the dummy is left wearing). `loadout.meanDamage`,
`loadout.meanProtection` and `loadout.hitLocations` read a cell.

Three things about a lane are worth knowing, because they are properties of the game
rather than of the fixture:

- **It cannot measure closer than 8 tiles.** Inside the messy-death range one solid
  torso hit ends a soldier outright, and the target is restored *between* shots, not
  during them. The lane refuses a closer distance and says why.
- **A shot does not always leave the barrel on the first order.** The game spends one
  getting the shooter into position - turning to face the target, raising the gun -
  exactly as it does for a player clicking a tile. `loadout.lane` orders again until
  the shot registers, up to a bound, and says so if it never does.
- **The seed fixes the rolls, not the whole world.** The same spec and seed give the
  same sequence of trigger pulls, but a merc who has just hit something is a slightly
  better shot next time, so a reproducibility check belongs early in a test.

## Running

They need the original game data (`game_dir` in your `ja2.json`) and the Python
tooling environment; `python tools/dev.py setup` installs the latter (it runs
`uv sync`, which creates `.venv` with Pillow and numpy from `uv.lock`).

```bash
python tools/dev.py e2e                                             # all, job-capped across agents
python tools/dev.py e2e tests/e2e/laptop_tour.lua --isolated        # one, from the repo root
python tools/ja2ctl.py run tests/e2e/laptop_tour.lua --isolated --show   # and watch it
ctest -L e2e -j8 --output-on-failure                                # raw form, from the build directory
```

`ctest` uses `.venv` when it exists, so re-run `cmake` once after `uv sync`
(`tools/dev.py setup` does both, in the right order).

`--isolated` gives every run a fresh home directory, so tests never see your
saves or each other's. Screenshots go to `--out` (CTest uses
`<build>/e2e-out/<test>/`); a failing script also leaves `failure.png` there.

## Writing a test

Find things by what they say rather than where they are, and wait for the game
instead of sleeping:

```lua
local campaign = require("lib.campaign")

campaign.newGame()                      -- ends in the laptop, popups closed
ja2.click("Web")
ja2.waitIdle()
ja2.click("A.I.M.")
ja2.waitFor("ASSOCIATION OF INTERNATIONAL MERCENARIES")
ja2.expect(ja2.state().money == 45000, "nothing spent yet")
ja2.screenshot("aim.png")
```

To find out what a screen offers, explore it in a live session:

```bash
python tools/ja2ctl.py start
python tools/ja2ctl.py eval 'require("lib.campaign").newGame()'
python tools/ja2ctl.py ui          # labelled, clickable elements
python tools/ja2ctl.py text        # all visible text with positions
python tools/ja2ctl.py shot        # look at it
```

Things worth knowing:

- **Popups come and go.** First-visit help screens and "You have new mail"
  appear depending on history; use `campaign.dismissHelp()`,
  `campaign.dismissLaptopPopups()` and `campaign.acceptMessageBox(pattern)`.
- **Ambiguous labels.** "History" is both a laptop program and an A.I.M. link:
  use `{text = "History", exact = true, within = {x = 0, y = 0, w = 110, h = 480}}`
  or `index = 2`.
- **Image-only widgets** have no text to find. Give them a name in C++
  (`button->SetName("Close help")`); see docs/automation.md.
- **Waiting.** `ja2.waitIdle()` understands fades, laptop page loads, AIM video
  calls, speech, the helicopter drop and walking. If a new kind of animation
  makes a test flaky, teach `NothingInFlight()` about it rather than adding
  `ja2.wait()`.
- **Pixels** (`ja2.pixelIs`, `ja2.waitPixel`) still work, but depend on the
  resolution; the tests run at 640x480.

## Resolution matrix

`ctest -L resolution` is the default sweep: every tour at 1920x1080. The other
resolutions (640x480, 1280x720, 2560x1080, 3440x1440) are opt-in with
`ctest -R resolution_` (or `ctest -L matrix`). Reserve the full matrix for
changes that alter how the game renders (a new menu, a texture/asset, the
renderer itself). Scripts take screenshots with
`shots.take(name[, golden])` (`lib/shots.lua`), which first calls
`ja2.assertInsideScreen()` (fails if any mouse region or button lies outside the
screen) and can register the shot for comparison with `golden/<res>/`. See
`golden/README.md`. Use `campaign.std{...}` to convert classic 640x480
coordinates (e.g. in `within=`) to the current screen.

## Layers (world zoom)

`tactical_layers.lua` and `tactical_layers_ui.lua` run tactical with the world as a layer of its own:
`ja2ctl run ... --res 2560x1440 --uiscale 2 --worldzoom 1` (headless only splits when `--worldzoom` is
given). Clicks and `ja2.gridPos` are in UI pixels; screenshots are the composite at window size
(UI size * UI scale), so 2560x1440 here. `ctest -L e2e` runs them at three combinations.
