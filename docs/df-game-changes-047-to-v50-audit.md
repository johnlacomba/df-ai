# Dwarf Fortress Game Changes: 0.47.05 → v50.x/v53.x (Steam Edition)

This document catalogs what changed in Dwarf Fortress between the original df-ai's last commit (701ea36, targeting DF 0.47.05) and the current Steam DF version (v50.x through v53.x). This is a *game changes audit only* — it does not assess what the current port covers or identify gaps. That comparison is a separate step.

## Table of Contents

1. [Removed / Replaced Systems](#1-removed--replaced-systems)
2. [New Systems](#2-new-systems-did-not-exist-in-047)
3. [Modified Systems](#3-modified-systems-exist-in-both-but-work-differently)
4. [Data Structure Changes (df-structures)](#4-data-structure-changes-df-structures)
5. [DFHack Tools — Removed / Replaced](#5-dfhack-tools--removed--replaced)
6. [Version Progression Summary](#6-version-progression-summary)
7. [Key Implications for a DFHack AI Plugin](#7-key-implications-for-a-dfhack-ai-plugin)

---

## 1. Removed / Replaced Systems

### 1.1 Labor Assignment System → Work Details

- The old per-dwarf labor toggle system (individual labor on/off per dwarf) is gone.
- Replaced by "Work Details" — a group-based labor system where you define labor categories (e.g., "Mining", "Farming") and assign dwarves to those groups.
- Three modes per work detail: "Only selected do this", "Everybody does this", "Nobody does this".
- By default all dwarves do everything; you restrict from there.
- **DFHack impact**: `autolabor` became fundamentally incompatible and was disabled/removed. `labormanager` was also broken — it was finely tuned to the old per-unit labor flags and does not mesh with Work Details. Dwarf Therapist required major rework.

### 1.2 Room System → Zone System

- In 0.47, rooms were created by placing furniture (bed, table, throne, coffin) then using `q` to define a room from the furniture and assign it to a dwarf.
- In v50, rooms are **zones** (bedroom, dining hall, office, tomb) created through the zone menu (`z`). You paint a zone, then assign furniture within it.
- Bedrooms require a bed in the zone. Offices require a chair/throne. Dining halls require a table.
- Tombs: coffins must now be in a Tomb zone. Each coffin needs its own 1-tile tomb zone for "public" burial; you cannot have multiple coffins in one large tomb zone.
- **DFHack impact**: New API functions added: `Gui::any_civzone_hotkey`, `Gui::getAnyCivZone`, `Gui::getSelectedCivZone`. The `buildings_other` struct added correct types for civzone building vectors. `civzone_type` enum got a `NONE` entry. `Buildings::setOwner` now takes `building_civzonest*`.

### 1.3 Kennel Building → Vermin Catcher's Shop + Animal Training Zone

- Kennels no longer exist. Tasks transferred to Vermin Catcher's Shops (catching vermin).
- Animal taming now requires designating an "animal training zone" and selecting animals from the creature/livestock menu.

### 1.4 Hospital Zone → Hospital Location

- Hospitals became a "location" (like taverns, libraries, temples) instead of a zone. They can span multiple areas, be named, and have associated occupations.

### 1.5 Building Designer Labor → Removed

- The "building designer" skill is gone entirely.
- Buildings are now constructed using the relevant material skill: masonry for stone, carpentry for wood, blacksmithing/weaponsmithing/armoring/metalcrafting for metal.

### 1.6 Stair Designation → Automatic

- You can no longer individually designate up-stairs, down-stairs, or up/down stairs.
- You select a vertical span (minimum 2 z-levels) and the game auto-determines which stair type goes where.
- The auto-logic is imperfect and can cause issues.

### 1.7 Medical/Health Screen → Removed (initially)

- The spreadsheet-style health screen showing all dwarves' injuries/treatments was removed at v50.01 launch. It has been partially restored in later versions.

### 1.8 Civilian Alerts → Removed (initially)

- The feature to force all civilians into a burrow was removed at v50.01 due to UI/time constraints. DFHack added this back via its own tools.

### 1.9 Notes/Hotkeys → Removed (initially)

- The ability to add map notes was removed from v50.01.

### 1.10 Door Lock/Pet-Proofing → Removed

- Cannot lock doors or set them as pet-impassable in v50 through the vanilla interface.

### 1.11 Individual Ammo Assignment → Removed

- Cannot designate specific ammo types for squads or hunters; dwarves grab from nearest stockpile.

### 1.12 Ruby DFHack Scripts → Removed

- Ruby language support for DFHack scripts was removed entirely due to issues with Ruby as an embedded language. Lua only.

---

## 2. New Systems (did not exist in 0.47)

### 2.1 Work Details System
Replacement for labor assignment (see §1.1 above).

### 2.2 Overlay Framework (DFHack)
- Entirely new DFHack architecture. Instead of pushing viewscreens onto the stack, widgets can draw directly onto existing viewscreens.
- Widgets inherit from `overlay.OverlayWidget`.
- Focus strings (e.g., `dwarfmode/Info/CREATURES/CITIZEN`) control when widgets appear.
- Free UI for enable/disable and repositioning. State persists across restarts.

### 2.3 Standing Orders for Petitions
- Automatic accept/reject for citizen petitions and residency petitions.

### 2.4 Portraits System (v50.13+)
- Procedurally assembled portraits for dwarves, elves, humans, and animal people.

### 2.5 SDL2 Engine (v50.09)
- Engine upgraded from SDL to SDL2, with many optimizations. Linux support improved.

### 2.6 Bolt Thrower Siege Engine (v53+)
- New defensive siege engine that fires regular bolts and can be freely rotated.

### 2.7 Battering Rams (v53+)
- Invaders can bring battering rams to destroy constructions and buildings.

### 2.8 Invader Intelligence (v53+)
- Invaders track death/trap locations and plan to avoid them.
- Invaders can dig through natural walls (trolls with picks).
- Invader army composition is more varied with different behavioral roles.
- Invaders can be led by historical military position holders.
- Flying invader behavior improved.

### 2.9 Lua Scripting for Procedural Generation (v52)
- Lua scripting affects procedural object generation: forgotten beasts, divine curses (vampires/werebeasts), divine items, necromancers, evil weather.

### 2.10 Adventure Mode (v51, restored)
- Adventure mode returned in v51.02 (January 2025) with overhauled character creation, updated interface, and new mythical content.

---

## 3. Modified Systems (exist in both but work differently)

### 3.1 Viewscreen Architecture
- In 0.47, DF used a deep stack of viewscreens (`viewscreen_dwarfmodest`, `viewscreen_joblistst`, etc.).
- In v50, there is generally **one viewscreen** (`dwarfmodest`) with a panel-based UI. Different interfaces are shown by changing mode/focus within that single viewscreen.
- Focus strings like `dwarfmode/Info/CREATURES/CITIZEN` identify the current sub-panel context.
- **DFHack impact**: Most old viewscreen hooks are broken. Plugins that relied on specific viewscreen types on the stack need rewriting. The overlay system is the new approach.

### 3.2 Trading/Caravans
- Trade is more like early DF: first trader arrives with minimal goods (couple of donkeys); you must earn a full caravan visit.
- Broker vs. Trader: in 0.47 the broker's Appraisal skill determined value display; in v50 the trader's skill matters.
- UI: moved from keyboard `q`-select-depot to click-based "Trade" button.

### 3.3 Military System
- Squad equipment: can now select by item type, specific item, or create/apply uniform templates including material and color.
- Raid missions: squads can go off-map to raid sites, rescue citizens, retrieve artifacts, explore ruins.
- Scheduling: selectable routines per squad broken up by month, quickly swappable.
- Soldiers could hoard rotten food in rooms (fixed in 50.10).
- Squad ammo assignment changes (see §1.11).

### 3.4 Workshop Construction
- Workshops now require the appropriate skill labor to build: carpenter for woodworking shop, mason for stoneworking shop, etc.
- Material-specific: carpentry for wood, masonry for stone, metalcrafting/blacksmithing for metal, engineering for mechanical.

### 3.5 Zone System (expanded)
- Zones are unlimited in size (0.47 had a 31×31 max).
- "Multi" mode: draw a rectangle over multiple rooms and each valid room becomes a separate zone.
- Locations (taverns, temples, libraries, guildhalls, hospitals) are assigned to zones via an "Assign location" submenu.
- Guilds and temples can be upgraded (guildhall to grand guildhall) via petition system.

### 3.6 Stockpile System
- Stockpile links (give/take) still exist.
- Quantum stockpiles still work but through minecart hauling routes.
- `copystock`, `loadstock`, `savestock` DFHack tools replaced by new `stockpiles` API.

### 3.7 Manager/Work Orders
- Work order conditions can be added (activate based on item count, or upon completion of other orders).
- If multiple conditions exist, all must be satisfied.
- Bug: selecting material type for a job must be done before the manager confirms the order, or any material will be used.
- Standing orders accessed via `y` (Labor menu) instead of `o`.

### 3.8 Embark System
- Embark points total: 1,504 (544 free after pre-spent basic equipment).
- Embark points can be changed via advanced world generation (max 10,000).
- 10 skill picks per dwarf.

---

## 4. Data Structure Changes (df-structures)

### 4.1 Major Canonicalization Effort

A massive rename pass was done to align df-structures field names with DF's internal names. All `unk`, `anon`, and placeholder names were changed.

### 4.2 Field Renames

| Old Name | New Name | Notes |
|----------|----------|-------|
| `job.item_category` | `job.specflag` | Now a union of flag fields, depends on job type |
| `plotinfo.main.selected_hotkey` / `in_rename_hotkey` | `plotinfo.main.hotkey_interface` | |
| `world_population_ref.depth` | `world_population_ref.layer_depth` | Changed from enum to integer |
| `world_raws.unk_v50_1/2/3` | `world_raws.text_set`, `music`, `sound` | |
| `plot_infost.unk_8` | `theft_intrigues` | |
| `plant_flags`: `is_burning`, `is_drowning`, `is_dead` | `unused_01`, `season_dead`, `dead` | Renamed to Bay12 names |
| `plotinfost.equipment` vectors | Same names | Converted from pointers to item refs |
| `squad.meeting_holder` | `meeting_holder_actor` and `meeting_holder_noble` | From unit pointer to two unit refs |
| `viewscreen_tradegoodsst.trade_reply`: `OffendedAnimal/Alt` | `OffendedBoth` / `OffendedAnimal` | |
| `toUpper` / `toLower` (MiscUtils) | `toUpper_cp437` / `toLower_cp437` | |

### 4.3 Enum Changes

- `specific_ref_type`: removed `BUILDING_PARTY`, `PETINFO_PET`, `PETINFO_OWNER` (alignment fix)
- `NONE` entries added to many enum types including `civzone_type` (affects C++ switch statements)
- `viewscreen_choose_start_sitest`: warning flags converted from series of bools to a proper bitmask

### 4.4 New Structures

- `viewscreen_new_arenast` added (v50.06)
- `building_civzonest`: `dir_x` and `dir_y` fields identified (archery range direction)
- `buildings_other`: correct types for civzone building vectors

### 4.5 Removed/Changed API Functions

| Function | Change |
|----------|--------|
| `Items::createItem` | Removed `growth_print` parameter (now auto-determined) |
| `dfhack.items.createItem` (Lua) | Same removal |
| `Gui::any_civzone_hotkey` | New |
| `Gui::getAnyCivZone` / `getSelectedCivZone` | New |
| `Gui::getAnyStockpile` / `getSelectedStockpile` | New, works through ZScreen layers |
| `Maps::getWalkableGroup` | New |
| `Units::getReadableName` | New (returns untranslated name) |
| `Buildings::completebuild` | Newly exposed module API |

### 4.6 Structural Corrections

- Collections of fields that were actually substructures (and vice versa) were corrected throughout.
- Army struct: squads vector type changed to `world_site_inhabitant`.

---

## 5. DFHack Tools — Removed / Replaced

| Old Tool | Status | Replacement |
|----------|--------|-------------|
| `autolabor` | Incompatible with Work Details | Partially re-enabled with limitations |
| `labormanager` | Broken, does not compile | Eventual return planned |
| `workflow` | Replaced | v50 vanilla work order conditions |
| `fortplan` | Removed | `quickfort run file.csv` |
| `gui/assign-rack` | Removed | — |
| `gui/automelt` | Removed | Overlay panel replacement |
| `gui/dig` | Removed | Merged into other tools |
| `gui/hack-wish` | Removed | — |
| `gui/no-dfhack-init` | Removed | — |
| `gui/stockpiles` | Removed | New `stockpiles` API |
| `masspit` | Removed | — |
| `resume` | Removed | `unsuspend` script |
| `ruby` (entire language) | Removed | Lua only |
| `show-unit-syndromes` | Removed | `gui/unit-syndromes` |
| `stocksettings` | Removed | New stockpiles API |
| `copystock/loadstock/savestock` | Removed | New stockpiles API |
| `title-version` | Removed | — |
| `warn-stuck-trees` | Removed | — |
| `gui/embark-anywhere` | Replaced | New version |

---

## 6. Version Progression Summary

| Version | Date | Key Changes |
|---------|------|-------------|
| **v50.01** | Dec 2022 | Steam launch. Fortress mode only. New UI/graphics. Many 0.47 features missing. |
| **v50.03–50.08** | 2023 | Stability fixes, crash fixes, army corruption fixes. |
| **v50.09** | 2023 | SDL → SDL2 engine upgrade. Linux support. |
| **v50.10** | 2023 | Civilian alerts restored, ammo fixes, soldier behavior fixes. |
| **v50.11–50.12** | 2023 | Continued fixes and improvements. |
| **v50.13** | 2024 | Portraits system added. |
| **v50.14–50.15** | 2024 | Final fortress mode updates before Adventure mode. |
| **v51.01–51.05** | 2024–2025 | Adventure mode beta and public release (Jan 2025). Overhauled character creation. |
| **v52.01–52.05** | 2025 | Lua scripting for procedural generation. Siege update prep. |
| **v53.01–53.14** | 2025–2026 | Siege Update. Battering rams, bolt throwers, smarter invaders, digging invaders, flying mount improvements. |

---

## 7. Key Implications for a DFHack AI Plugin

1. **Viewscreen navigation is completely different.** Any code that pushes/pops viewscreens or hooks specific viewscreen types needs full rewrite. Use focus strings and the overlay system instead.
2. **Labor management is fundamentally changed.** Must use Work Details API instead of per-unit labor flags.
3. **Room/zone management is unified.** All room types are now civzones. Use the new `getSelectedCivZone` / `getAnyCivZone` API. `Buildings::setOwner` takes `building_civzonest*`.
4. **Structure field names changed massively.** Any code referencing `unk_*` or `anon_*` fields needs updating to canonical names. Many type changes (pointers to refs, bools to bitmasks, enums to ints).
5. **Hospital is a location, not a zone.** Different creation and management path.
6. **Kennel building gone.** Animal training is zone-based.
7. **Building construction skill mapping changed.** No more building designer; must match material to skill.
8. **Stockpile API replaced.** Old copy/load/save tools gone; new unified stockpiles API.
9. **Many enum types gained NONE entries.** C++ switch statements on these enums will need default/NONE cases.
10. **Stair designation is automatic.** Cannot programmatically designate specific stair types the old way.

---

## Appendix A: Original df-ai System Catalog (at commit 701ea36)

For reference, these are all the game systems the original df-ai managed:

### Core Subsystems

| System | Key Files | What It Manages |
|--------|-----------|-----------------|
| **Population** | `population.cpp`, `population_military.cpp`, `population_nobles.cpp`, `population_occupations.cpp`, `population_pets.cpp`, `population_death.cpp`, `population_justice.cpp` | Dwarf labor, military, nobles, occupations, pets, deaths, justice |
| **Stocks/Production** | `stocks.cpp`, `stocks_manager.cpp`, `stocks_equipment.cpp`, `stocks_farm.cpp`, `stocks_forge.cpp`, `stocks_queue.cpp`, `stocks_find.cpp`, `stocks_update.cpp`, `stocks_detect.cpp`, `stocks_trade.cpp` | 130+ item types, manager orders, farming, forging, equipment |
| **Plan/Construction** | `plan.cpp`, `plan_setup.cpp`, `plan_construct.cpp`, `plan_find.cpp`, `plan_task.cpp`, `plan_assign.cpp`, `plan_smooth.cpp`, `plan_persist.cpp`, `plan_priorities.cpp`, `plan_cistern.cpp` | 20+ room/building types, blueprints, cisterns, furniture |
| **Camera** | `camera.cpp` | Unit following, movie recording, FPS monitoring |
| **Trade** | `trade_helpers.cpp`, `trade_manager.cpp` | Caravan trade negotiation and execution |
| **Military** | `military.cpp`, `population_military.cpp` | Squad creation, equipment, attack orders, threat assessment |
| **Embark** | `embark.cpp` | Automated embark site selection |
| **Blueprint** | `blueprint.cpp`, `blueprint_*.cpp` | Template-based fortress layouts |
| **Configuration** | `config.cpp` | Persistent AI behavior settings |

### Exclusive Callbacks (Screen Interaction Queue)

- `MilitarySetupExclusive` — squad management screens
- `AssignNoblesExclusive` — noble position assignment
- `AssignOccupationExclusive` — tavern/library/temple occupation assignment
- `ManagerOrderExclusive` — job manager screen operation
- `PerformTradeExclusive` — caravan trading
- `PlanSetup` — blueprint application during fortress startup
- `EmbarkExclusive` — embark phase automation
- `RestartWaitExclusive` — post-embark wait handling

### System Statistics

- **Managed room/building types**: 20+ (corridor variants, barracks, bedrooms, cemetery, cistern, dining hall, farm, furnace, garbage dump, infirmary, jail, location, noble room, outpost, pasture, pit cage, release cage, stockpile, trade depot, windmill, workshop)
- **Tracked item types**: 130+
- **Tracked occupations**: Tavern keeper, tavern performer, library scholar, library scribe, temple performer
- **Enemy threat categories**: 11 (megabeast, semi-megabeast, forgotten beast, titan, unique demon, demon, night creature, undead, active invader, marauder, uninvited visitor)

---

## Sources

- [Steam Community Guide: List of Undocumented Changes and Problems in Premium DF](https://steamcommunity.com/sharedfiles/filedetails/?id=2901569443)
- [DFHack Development Changelogs](https://docs.dfhack.org/en/50.12-r2/docs/NEWS-dev.html) (50.10-r1, 50.12-r2, 50.15-r2)
- [DFHack Removed Tools](https://docs.dfhack.org/en/50.13-r5/docs/about/Removed.html)
- [DFHack Overlay Dev Guide](https://docs.dfhack.org/en/50.11-r5/docs/dev/overlay-dev-guide.html)
- [df-structures changelog.txt](https://github.com/DFHack/df-structures/blob/master/changelog.txt)
- [DFHack Zone-Building PR #2662](https://github.com/DFHack/dfhack/pull/2662)
- [DF Wiki: Release information](https://dwarffortresswiki.org/index.php/Version_history) (50.01, 52.01, 53.01)
- [DF Wiki: Zone](https://dwarffortresswiki.org/index.php/Zones), [Work Orders](https://dwarffortresswiki.org/index.php/Work_orders), [Labor](http://www.dwarffortresswiki.org/index.php/Labor), [Location](http://www.dwarffortresswiki.org/index.php/Location), [Stairs](https://www.dwarffortresswiki.org/index.php/Stairs), [Standing Orders](https://dwarffortresswiki.org/index.php/Standing_orders)
- [All Differences between DF Classic and DF Steam - Pro Game Guides](https://progameguides.com/dwarf-fortress/all-differences-between-dwarf-fortress-classic-and-dwarf-fortress-steam/)
- [DFHack autolabor/labormanager Discussion #2938](https://github.com/DFHack/dfhack/discussions/2938)
- [Steam: Work Details and Work Orders Guide](https://steamcommunity.com/sharedfiles/filedetails/?id=2920669655)
