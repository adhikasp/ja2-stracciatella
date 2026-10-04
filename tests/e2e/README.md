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
| `campaign_state.lua` | Author a campaign state (day, money, towns, roster, quests) on the live globals, assert it and the status model, then save/load it (see [docs/plan/e2e-campaign-state.md](../../docs/plan/e2e-campaign-state.md)) |
| `battle_campaign.lua` | Walk into a controlled town with townsfolk, then step into a tactical battle there (runs at 1920x1080) |
| `map_screen_tour.lua` | Map screen: pause, inventory, options, laptop |
| `video_switch.lua` | Runtime video changes (`ja2.setVideo`) through four modes on tactical, map and laptop |
| `video_options.lua` | The Video options screen: change and apply resolution without a restart |
| `credits_parity.lua` | Native credits parity tour by element id (docs/ui/credits.md); legacy fallback below 1280x720 |
| `tactical_parity.lua` | Native tactical HUD parity tour by element id (docs/ui/tactical.md): squad bar, inventory, item description, money, log, the action, door and pick-up menus |
| `native_ui.lua` | Native UI runtime on legacy screens: native message box (mouse, focus, shortcuts), toasts, UI scales, a view model |
| `legacy_script.txt` | The old line-based `-uitest` format still works |
| `check_determinism.py` | Same script and seed, twice: identical screenshots |
| `check_sessions.py` | Two `ja2ctl` sessions side by side stay independent |

`lib/campaign.lua` has the shared steps (new game, hire from A.I.M., land,
dismiss popups, and the campaign-state helpers `stage`, `at`, `assertState`,
`enterSector`, `enterTown`, `stepIntoBattle`). `lib/battle.lua` has the battle
fixtures and orders (stage a fight, select a merc, shoot, end the turn) for the
tactical battle e2e track.

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
