# Laptop — functional spec (M1)

> The parity contract for Phase 6 of [native-modern-game.md](../plan/native-modern-game.md). Every row below is
> either covered by the native laptop's parity tour (M4) or listed in §8 as deliberately dropped or changed.

| | |
|---|---|
| Legacy code | `src/game/Laptop/` (entry: `EnterLaptop()`, frame: `LaptopScreenHandle()`, exit: `ExitLaptop()`; per-mode `Enter*/Render*/Handle*/Exit*`, dispatched on `guiCurrentLaptopMode`) |
| Screen id | `LAPTOP_SCREEN` (`ja2.state().laptopMode` = `LaptopMode` in `Laptop.h`) |
| Reached from | new game (straight after the difficulty confirmation), map screen Laptop button / `L`, tactical `L` |
| Returns to | `MAP_SCREEN` (`SetLaptopExitScreen`), or the screen it came from |
| Status | wireframes approved by the owner (2026-10-02); implementation in progress |

## 1. How it was audited

- Driven audit: `tests/e2e/manual/phase6_audit.lua` (new game, `ja2.debug("bookmarks")` sets every bookmark and
  opens Bobby Ray's, then walks every program and site, hires Barry, and dumps text and clickable elements with a
  screenshot per state). States reached: desktop, mail list and reading, files, history, personnel (empty and with a
  hire), finances, bookmarks menu, A.I.M. home / sort / mug-shot index / member / contact / hire / transfer / policies /
  history / alumni, M.E.R.C. home / files / account, I.M.P. home / about us, Bobby Ray's home / guns / order form,
  florist home / gallery, insurance home, funeral home. The A.I.M. links page and the I.M.P. quiz were read from code.
- Code read: `Laptop.cc` (modes, programs, bookmarks, exit, keys, rain delay, "World Wide Wait"), every site's
  `Enter/Render/Handle` and its button callbacks, `EMail.cc`, `Files.cc`, `Finances.cc`, `History.cc`, `Personnel.cc`.
- Text sources (all existing data, none baked into the native pages): `translation-<lang>.json`
  (`pLaptopIcons`, `pBookMarkStrings`, `pWebPagesTitles`, `CharacterInfo`, `VideoConfercingText`, `AimPopUpText`,
  `MercInfo`, `BobbyRText`, `BobbyROrderFormText`, `pFinanceSummary`, `pFinanceHeaders`, `pTransactionText`,
  `pPersonnelScreenStrings`, `pPersonnelTeamStatsStrings`, `pImpButtonText`, ...), the game's `.edt` files
  (`email.edt`, `aimbios.edt`, `mercbios.edt`, `files.edt`, `ris.edt`, `quests.edt`, I.M.P. quiz and policy text),
  `mercs-AIM-listings.json`, `mercs-MERC-listings.json`, `bobby-ray-inventory-*.json`, `imp.json`,
  `shipping-destinations.json`.
- Images: merc big faces (`faces/bigfaces/NN.sti`, native `face-<n>`), item big pictures (`BIGITEMS`, native
  `shop-item-<n>`, uplifted 2x), I.M.P. portraits (faces 200+), Speck's and the A.I.M. video frames, flower pictures.
  Site backgrounds, logos, wood/marble textures and the laptop bezel are replaced by native styling (§8).

## 2. The shell (sir-FER OS)

| # | Information / action | Legacy | Native (wireframe) |
|---|---|---|---|
| I1 | Date and time | bottom-left of the bezel "Day 1, 01:00" | system bar, top right |
| I2 | Balance | under Financial icon | system bar |
| I3 | Mercs on the team | under Personnel icon "Mercs: N" | system bar |
| I4 | New mail / new file indicators | mail and folder icons, blinking; "You have new mail..." popup on entry | dock badges with counts; toast instead of the popup; notification list on the desktop |
| I5 | Power and hard-drive lights | bezel LEDs (HD flickers while a page "loads") | dropped (§8) |
| A1 | Open a program: E-mail, Web, Files, History, Personnel, Financial | left icon column (`CreateLaptopButtons`) | dock; `F1`–`F6` |
| A2 | Close the current program | window X (minimise animation) | dock switch; Esc returns to the desktop |
| A3 | Shut down → map | "Shut Down" (`BtnOnCallback` → `HandleExit`); power-down zoom animation | dock "Close" / system bar "Map", `Esc` from the desktop |
| A4 | First exit of a new game without an I.M.P. character | `HandleExit` schedules the "haven't made an I.M.P. character" mail (day 2, 8–12h) | same call |
| A5 | First exit of a new game with no merc hired | stays on laptop / map transition rules in `LeaveLapTopScreen` (`gfAtLeastOneMercWasHired`, `gfNewGameLaptop`) | same rules |
| A6 | `Esc` | closes new-mail box, then delete-mail box, then bookmarks, else exits | closes the topmost popup, then the program, then exits |
| A7 | `Tab` / `Ctrl+Tab` | cycle open programs (`HandleAltTabKeyInLaptop`) | same |
| A8 | `Alt+X` | quit-game prompt | same |
| A9 | `H` | laptop help screen (`HELP_SCREEN_LAPTOP`; first-visit help also auto-opens) | help button + `H` |
| A10 | Cheats: `+`/`-` money, `Alt+B` open Bobby Ray's, `Ctrl+B` broken link | cheat level only | same, cheat level only |
| S1 | Desktop (`LAPTOP_MODE_NONE`) | wallpaper | desktop with program tiles, web shortcuts, notifications |
| S2 | Sleep/logout | no sleep mode: the laptop is a screen; time is paused while it is open (game clock does not run in `LAPTOP_SCREEN`); leaving goes to the map | same; nothing to add |

## 3. Web browser

| # | Item | Legacy | Notes |
|---|---|---|---|
| W1 | Bookmarks | "Web" opens a drop-down list of the bookmarks set so far (`LaptopSaveInfo.iBookMarkList`), plus Cancel; tooltips per site | Native: an always-visible bookmark bar. Sites appear only when their bookmark is set (by mails and events: A.I.M. from the start, Bobby Ray's, M.E.R.C., I.M.P., florist, insurance, funeral) |
| W2 | New bookmark notice | "Click Web again for bookmarks" helper (`DisplayWebBookMarkNotify`) | `new` badge on the bookmark |
| W3 | "World Wide Wait" | first visit to a site shows a loading bar for a few seconds (`fLoadPendingFlag`, `fFastLoadFlag` after first visit); sub-pages load too | Kept as a short accent loading line under the address bar (owner choice below) |
| W4 | Rain delay | rain or thunderstorm: an "internet connection slow" message box before the page (`IsItRaining`, `giRainDelayInternetSite`) | keep: same message as a modal |
| W5 | Page title | window title "sir-FER 4.0 - <site page>" (`pWebPagesTitles`) | address bar + system bar subtitle |
| W6 | Browser history | none: no back/forward; sub-pages have their own Home/Back links | Native adds Back/Forward (`Alt+Left/Right`) over the visited-mode list (new behaviour, §8) |
| W7 | M.E.R.C. broken link | once the M.E.R.C. server has gone down and not come back: "URL not found" page (`LAPTOP_MODE_BROKEN_LINK`) | same page, native |

## 4. Programs and sites (per section: information, actions, states)

### 4.1 E-mail (`EMail.cc`)
- List: 18 per page with page arrows; columns From, Subject, Day; unread in bold with an envelope icon; read
  state icon column. Sort by sender, subject, date received, read (`SortMessages`) by clicking the headers.
- Reading: popup with From, Subject, Day, body (paged when long, `MAX_EMAIL_MESSAGE_PAGE_SIZE`), previous/next page,
  delete (trash icon → confirmation "Delete mail?" yes/no), X closes. Opening marks it read.
- Mails can carry effects: payment mails (Enrico) credit money, I.M.P. results, R.I.S. report adds a file, Bobby
  Ray's/M.E.R.C./florist/insurance/funeral mails set bookmarks; the "new mail" box appears on arrival or entry.
- Native: list + reading pane side by side; effects shown as an attachment line (e.g. "Payment received").

### 4.2 Files (`Files.cc`)
- List of files (Recon report, Enrico's notes, plans) left; content right, paged, with pictures for some
  (R.I.S. report). New-file indicator on the bezel. Native: same two panes, pages by scroll.

### 4.3 History (`History.cc`)
- Log of quest and event records (`quests.edt`): Day, Location, Event; red for open quests; 22 per page,
  page arrows, "Page x / y", "Day a - b". Native: one scrolling table with day separators.

### 4.4 Finances (`Finances.cc`)
- Page 1 summary (`pFinanceSummary`): yesterday's income, other deposits, debits, balance at day's end; today's
  same; current balance; forecasted income; projected balance. Following pages: ledger, 17 rows per page — Day,
  Transaction (`pTransactionText`), Credit, Debit, Balance; first/prev/next/last page buttons.
- Native: summary as cards above, ledger scrolling below, yesterday panel and a balance chart beside it (chart is new).

### 4.5 Personnel (`Personnel.cc`)
- Current team / Departures toggle; portrait grid with arrows when more than fit; selected merc's face, name,
  stats (health, agility, dexterity, strength, leadership, wisdom, level, marksmanship, mechanical, explosives,
  medical, with the latest increase), employment (daily cost, contract, remaining contract, total cost, total service,
  medical deposit, salary owing for M.E.R.C.), record (kills, assists, hit %, battles, times wounded), skills;
  inventory view with a scroll bar; team summary (daily cost, highest/lowest cost) and team stat min/avg/max table.
  Departed: dead (greyed portrait), fired, married, contract expired, quit.
- Native: roster left with team totals, detail right with Stats / Employment / Inventory tabs.

### 4.6 A.I.M. (`AIM*.cc`)
- Home: logo, ads banner (rotating), links Members, Alumni ("Archives"), Policies, History, Links.
- Members → Sort page (`AIMSort.cc`): sort by Price, Experience, Marksmanship, Medical, Explosives, Mechanical,
  ascending/descending; to Mug-shot index, Stats, Archives.
- Mug-shot index (`AIMFacialIndex.cc`): 40 faces, overlays for away / dead / hired.
- Member page (`AIMMembers.cc`): face, name, stats, fees (1 day / 1 week / 2 weeks, `CharacterInfo`), medical
  deposit, optional gear (item pictures, price), bio "Additional Info", Previous / Contact / Next.
- Contact (video conference): connecting animation, merc speaks (quote audio + subtitles), answering machine if
  away ("Leave message" records a message: merc emails back later), refusal if hostile/dead. Hire: contract
  length, No/Buy equipment, contract charge, Transfer Funds → "Electronic funds transfer successful" or
  "insufficient funds" / full team (18); then "<name> should arrive..." message box. Hang up / X.
- Policies (agree / disagree gate, table of contents, pages), History (pages), Links (to Bobby Ray's, funeral,
  florist, insurance: sets nothing new, just navigates), Alumni (faces of former members with bio popups).
- Native: member grid with sort chips (replaces sort page + index), member page with the hire panel docked right.

### 4.7 M.E.R.C. (`Mercs*.cc`)
- Home: Speck's talking-head video and quotes (`Speck_Quotes.h`, `mercs-MERC-listings.json` conditions), "open an
  account" (Mercs_No_Account: Open Account / Cancel), "view files" (once an account exists).
- Files: one merc per page, face, stats, bio, salary per day, Hire (no contract length, paid daily), Prev/Next, Back;
  availability by days and total spending; "Hired" / "Deceased" / "Unavailable", 18-merc limit.
- Account: per-merc days and amount owed, Authorize payment; server going down/up events; Speck's late-payment
  behaviour.
- Native: Speck panel left (video + quote + account), all files as rows with Hire.

### 4.8 I.M.P. (`CharProfile.cc`, `IMP_*.cc`)
- Home: activation code field (`imp.json` codes, wrong-code message), About Us.
- Main page: Begin, Personality, Attributes, Portrait, Voice, Finish in order; unavailable steps disabled; cost
  $3,000 (`IMP_Confirm`), only if a character can still be made.
- Begin: full name, nickname, gender (text input, Enter/Esc).
- Personality quiz: 16 questions, 4–8 answers each; select, confirm; Previous/Next among answered; Start Over.
  Answers decide personality and attitude (`IMP_Personality_Quiz.cc`), plus Specialties (`IMP_SkillTraits.cc`).
- Attributes: 40 bonus points, sliders with +/− for the 5 attributes and 5 skills (zero-skill box), finish confirm.
- Portrait and voice: prev/next through `imp.json` lists, voice sample plays. Finish: review, Start over,
  Done → confirm payment → the character is created (`IMP_Compile_Character`) and arrives like a hire.
- Native: step bar across the flow; quiz one question per page with numbered answers (keys 1–8).

### 4.9 Bobby Ray's (`BobbyR*.cc`)
- Home (closed until the opening mail; then Guns, Ammo, Armor, Misc., Used). Category pages: 4 items per page —
  big picture, name, Cost, Weight, In stock, description, and Cal/Mag/Rng/Dam/ROF; click to add one, right-click
  to remove one; "Qty on Order"; Previous/More items; Home; Order Form; subtotal. Max 10 items per order; out of
  stock messages.
- Order form: Qty, weight, item, unit price, total; delivery location drop-down (`shipping-destinations.json`,
  only reachable airports), shipping speed (overnight / 2 days / standard, cost per kg), package weight (min. 2 kg),
  sub-total, S&H, grand total; Clear Order, Accept Order → confirm "send to <city>?" → money taken, shipment
  scheduled (arrives at the airport; Pablo may steal); Back, Home, Shipments (recent shipments list).
- Native: list with steppers and a cart side panel; order form as table + delivery/speed/totals panels.

### 4.10 United Floral Service (`Florist*.cc`)
- Home, Gallery (flowers with name, price, description, pages), Order form (bouquet, delivery town drop-down,
  next-day / when-convenient, card message text with Card Gallery, name/billing, Send → cost, mail and loyalty
  effects; Clear, Back). Native: own identity (`--site-florist`), same flow.

### 4.11 Malleus, Incus & Stapes Insurance (`Insurance*.cc`)
- Home (questions, links), Info (pages), Comments, Contract: per hired merc, cost, Buy/Cancel/extend
  (`pTransactionText` insurance entries), "no mercs" popup. Claims arrive by mail when an insured merc dies.

### 4.12 McGillicutty's Mortuary (`Funeral.cc`)
- Home with service links (send flowers, casket & urn, cremation, pre-funeral planning, etiquette); every link shows
  the "we're closed due to a death in the family" sign (`SelectRipSignRegionCallBack`). Pure content.

## 5. Popups and modals

| # | Popup | Opened by | Result |
|---|---|---|---|
| P1 | You have new mail | entering with unread mail, mail arriving | Yes opens the mail, No closes |
| P2 | Delete mail? | trash in the reader | yes deletes |
| P3 | Rain delay | first web page while raining | OK continues |
| P4 | Transfer successful / insufficient funds / full team | A.I.M. hire | OK |
| P5 | "<name> should arrive at the drop-off point" | after a hire | OK |
| P6 | Confirm Bobby Ray's order / can't afford | Accept Order | Yes/No |
| P7 | I.M.P. confirm payment, start over, wrong code | I.M.P. flow | Yes/No |
| P8 | Laptop help | first visit, `H` | close, "don't show again" |

## 6. Sounds, animations and timing

Power-up and power-down sounds and the zoom transition; HD light flicker; page loading wait; A.I.M. video
connecting static and merc speech; Speck video and speech; I.M.P. voice samples; minimise/maximise title-bar
animations; blinking new-mail icon. Speech and quotes must stay (they are content); the rest becomes design-system
motion (`--t-slow` panel transitions).

## 7. Edge cases

- [x] No mercs (Personnel shows "Current Team (0)"), no mail, no files beyond the R.I.S. report
- [ ] 18 mercs (hire limit messages on A.I.M. and M.E.R.C.), 10-item Bobby Ray's limit, long ledgers (pages → scroll)
- [ ] Dead mercs (A.I.M. "Deceased", departed list), mercs away on assignment (answering machine)
- [ ] M.E.R.C. server down (broken link), Bobby Ray's not open yet, Speck's unpaid account
- [ ] Rain delay; insufficient funds on every purchase path
- [ ] Long translations: German page titles, Russian quiz answers (quiz answers wrap)
- [ ] 1280x720 to 3440x1440: the wireframes hold at all three sizes (no layout-audit findings)
- [ ] Keyboard only: F1–F6, Tab, Esc, quiz answers by number, Enter confirms

## 8. Deliberately dropped or changed

Owner decisions on the M2 wireframes (2026-10-02, `native-phase-6-wireframes/` on `pr-screenshots`):
- Every wireframe is approved as shown: the shell (system bar, dock, desktop), e-mail, A.I.M. members and member page
  with the docked hire panel, M.E.R.C., the I.M.P. quiz, Bobby Ray's shop and order form, finances and personnel.
- The sites that were not mocked (florist, insurance, mortuary, the remaining A.I.M. and I.M.P. pages, files, history)
  follow the same patterns: Night Ops shell, the site's own `--site-*` identity, content from the text data.
- Faces and other content art may scale fractionally (the 1dp layout scale), as long as the aspect ratio is kept;
  they are never stretched non-uniformly.
- The changes in the table below are approved.

| Item | Decision | Reason | Approved by |
|---|---|---|---|
| Laptop bezel, LEDs, desk wallpaper | dropped | full-screen OS per the plan; status moves to the system bar | owner, 2026-10-02 |
| Program windows minimise/maximise animation | replaced by panel transitions | | owner, 2026-10-02 |
| Bookmark drop-down | always-visible bookmark bar | one click instead of two | owner, 2026-10-02 |
| Back/Forward in the browser | added | modern browser expectation; within visited modes only | owner, 2026-10-02 |
| A.I.M. sort page + mug-shot index | merged into one member grid with sort chips | same functions, one page | owner, 2026-10-02 |
| Paged lists (mail, ledger, history, Bobby Ray's 4-per-page) | scrolling lists | modern displays fit more; page keys kept | owner, 2026-10-02 |
| Site art (logos, wood, marble, wallpapers) | native per-site styling (`--site-*` tokens) | page content from text data, no baked images | owner, 2026-10-02 |

## 9. Parity tour

`tests/e2e/laptop_parity.lua` (to write in M4): open each program by id (`laptop.app.*`), read and delete a mail
(assert read flag and count), hire from A.I.M. by `aim.hire.transfer` (assert team, balance, transaction), hire from
M.E.R.C. (assert account), order from Bobby Ray's (assert balance, shipment scheduled), make an I.M.P. character
(assert profile), buy insurance, send flowers; check finance ledger rows and personnel stats against game state.
