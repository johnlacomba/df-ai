---
title: "feat: Port df-ai to Dwarf Fortress Steam / DFHack 53.x"
type: feat
status: active
date: 2026-05-27
deepened: 2026-05-27
---

# feat: Port df-ai to Dwarf Fortress Steam / DFHack 53.x

## Overview

Port the df-ai DFHack plugin from Dwarf Fortress 0.47.05 (classic) / DFHack 0.47.05-r7 to Dwarf Fortress 53.14 (Steam) / DFHack 53.14-r2. The MVP goal is an autonomous AI that can embark, dig out a fortress, place furniture, and keep dwarves fed and hydrated through farming, hunting, and brewing. Non-essential subsystems (lockstep mode, CMV recording, weblegends, advanced military, criminal justice) are deferred to post-MVP.

---

## Problem Frame

df-ai has not been updated since October 2022 and targets the last "classic" DF release (0.47.05). The Steam/Premium release (December 2022 onward, now at v53.14) introduced:
- A complete UI rewrite (mouse-driven, replacing keyboard-centric menus)
- 64-bit only architecture (dropping 32-bit)
- Renamed DFHack globals (`ui` → `plotinfo`, `ui_sidebar_menus` → `game`)
- Restructured df-structures (`world->manager_orders` → `world->manager_orders.all`, many `T_*` types promoted)
- New game systems (Work Details replacing per-dwarf labors, location-based rooms, messenger/diplomacy, siege overhaul)
- ~100+ changed or removed viewscreen types

The plugin's core UI automation system (`ExclusiveCallback`) simulates keystrokes to navigate DF menus. This approach is fundamentally broken against the new UI and must be replaced with direct DFHack API calls wherever possible.

---

## Requirements Trace

- R1. Plugin compiles against DFHack 53.14-r2 on macOS
- R2. Plugin loads into DFHack and registers the `ai` command without crashing
- R3. AI can perform automated embark (world generation + site selection + embark)
- R4. AI generates a fortress blueprint and designates digging
- R5. AI constructs workshops, furnaces, and places furniture as rooms are dug out
- R6. AI establishes food production (farm plots, crop selection, brewing)
- R7. AI keeps dwarves alive (food, drink, basic shelter assignment)
- R8. AI ensures labors are assigned (via delegation to DFHack's `autolabor` plugin, which handles Work Details)
- R9. Plugin persists state across save/load cycles

---

## Scope Boundaries

- Lockstep mode (hooks.cpp SDL/timing patches) — not needed for MVP
- CMV movie recording — not needed for MVP
- Weblegends web UI integration — not needed for MVP
- Advanced military (squad equipment, kill orders, mercenary recruitment) — basic defense only
- Criminal justice system — deferred
- Trade caravan automation — deferred (dwarves survive on internal production)
- Messenger/diplomacy system (new in 53.13) — deferred
- Noble apartment allocation beyond basic needs — deferred
- Cavern exploration beyond what's needed for basic resources — deferred
- 32-bit build support — DF Steam is 64-bit only
- Windows/Linux build — plan targets macOS; other platforms follow the same code changes

### Deferred to Follow-Up Work

- Trade system rewrite: separate PR after MVP confirms fort survival
- Full military system: separate PR for squad management, equipment, defensive strategy
- Lockstep mode: separate PR, requires platform-specific hook rewrite for 64-bit
- Weblegends integration: separate PR after core loop is stable

---

## Context & Research

### Relevant Code and Patterns

- `df-ai.cpp` — plugin entry point, lifecycle hooks, singleton AI management
- `ai.h` / `ai.cpp` — central AI class owning 5 subsystems (Population, Plan, Stocks, Camera, Trade)
- `exclusive_callback.h` / `exclusive_callback.cpp` — coroutine-based UI automation (Boost.Coroutine2)
- `event_manager.h` / `event_manager.cpp` — periodic/state/exclusive callback dispatch
- `plan.cpp` / `plan_construct.cpp` / `plan_task.cpp` — fortress layout, digging, construction
- `stocks.cpp` / `stocks_queue.cpp` / `stocks_farm.cpp` / `stocks_manager.cpp` — production management
- `population.cpp` / `population_nobles.cpp` — citizen tracking, labor management
- `embark.cpp` — world gen and site selection (heavily uses ExclusiveCallback)
- `camera.cpp` — viewport control
- `hooks.cpp` — lockstep mode (SDL patching) — to be stubbed out
- DFHack's `plugins/examples/skeleton.cpp` — modern plugin lifecycle reference
- DFHack's `plugins/orders.cpp` — programmatic manager order creation reference
- DFHack's `plugins/autofarm.cpp` — modern farm automation reference

### External References

- DFHack 53.14-r2 API docs: https://docs.dfhack.org/en/stable/
- DFHack dev changelog (global renames): https://docs.dfhack.org/en/stable/docs/NEWS-dev.html
- DFHack build dependencies: https://docs.dfhack.org/en/stable/docs/dev/compile/Dependencies.html
- DFHack Buildings module: `library/include/modules/Buildings.h`
- DFHack Military module (new): `library/include/modules/Military.h`
- df-structures changelog: https://github.com/DFHack/df-structures/blob/master/changelog.txt

---

## Key Technical Decisions

- **Replace ExclusiveCallback UI navigation with direct API calls (MVP-critical subclasses enumerated)**: The old UI simulation approach is incompatible with DF Steam's mouse-driven interface. Each MVP-critical ExclusiveCallback subclass has a specific replacement strategy:
  - `ManagerOrderExclusive` (`stocks_manager.cpp`) → direct `world->manager_orders.all.push_back()` — no UI needed
  - `ConstructStockpile` (`plan_construct.cpp:1221`) → `Buildings::allocInstance` with `building_type::Stockpile` + direct `building_stockpilest` field manipulation — no UI needed
  - `ConstructActivityZone` (`plan_construct.cpp:1457`) → `Buildings::allocInstance` with `building_type::Civzone` + `Buildings::constructAbstract` + direct zone/location field manipulation. The old `dwarfmode/Zones` focus string and `CIVZONE_*` key sequences are broken in Steam UI. Location assignment within zones (tavern, library, temple, guildhall) requires investigation — may need updated UI simulation or direct struct manipulation
  - `AssignNoblesExclusive` (`population_nobles.cpp:48`) → direct entity position assignment via `historical_entity->positions` manipulation — no UI needed
  - `MasonChairJobExclusive` (`stocks_queue.cpp:24`) → direct manager order creation — no UI needed
  - `BlueprintSetupExclusive` (`plan_setup.cpp:32`) → investigate; may not be needed if blueprint generation runs internally without UI
  - **Deferred (not MVP):** `AssignOccupationExclusive`, `CheckPetitionsExclusive`, all trade-related ExclusiveCallbacks

- **Keep ExclusiveCallback coroutine framework for embark only**: The embark flow (title screen → world gen → site selection → embark config) still requires UI navigation. The Boost.Coroutine2 coroutine infrastructure in `exclusive_callback.h` is framework-level code that doesn't depend on DF internals — it is preserved. Only the DF-specific viewscreen type checks and key sequences in `embark.cpp` need rewriting for Steam's new screens. All other ExclusiveCallback *subclasses* are replaced with direct API calls per above.

- **Stub non-MVP subsystems rather than deleting them**: Wrap lockstep mode, weblegends, dfplex, trade, and advanced military in compile-time guards (`#ifdef DF_AI_FULL`) or runtime checks. This preserves the code for future phases while keeping the MVP build clean. Note: weblegends and dfplex are integrated into `df-ai.cpp` and `event_manager.cpp` (not just their own files) — stubbing requires changes in those core files too.

- **Delegate labor management to DFHack's `autolabor`/`labormanager` plugin**: Rather than building a Work Details management system from scratch (which requires understanding an underdocumented API), the MVP will delegate labor assignment by running `enable autolabor` (df-ai already did this in 0.47). If `autolabor` works in 53.x (it was rewritten for Work Details), this eliminates the need for df-ai to manage Work Details directly. The population manager focuses on citizen tracking, noble assignment, and bedroom assignment. If `autolabor` proves broken in 53.x, fall back to direct `unit->status.labors[]` manipulation as an interim measure — per-unit labor flags may still function even if Work Details is the primary UI mechanism.

- **Pin to DFHack 53.14-r2**: Target a specific version to avoid chasing a moving target. DFHack releases are tightly coupled to DF versions; version bumps are a future maintenance task.

- **Build as an in-tree plugin**: Clone df-ai into `dfhack/plugins/df-ai/` and build with DFHack's CMake. This is the supported approach and matches the existing CMakeLists.txt structure.

---

## Open Questions

### Resolved During Planning

- **Can building placement still use the Buildings API directly?** Yes — `Buildings::allocInstance`, `setSize`, `constructWithItems`, and `constructWithFilters` are unchanged in 53.x. df-ai already uses these in `plan_construct.cpp`.

- **Can manager orders be created programmatically?** Yes — `world->manager_orders.all.push_back(order)` works. The `orders` plugin and `autoslab` plugin demonstrate this pattern. This eliminates the need for `ExclusiveCallback`-based order creation via `viewscreen_jobmanagementst`.

- **Does Boost.Coroutine2 still work with modern DFHack?** Yes — DFHack still links Boost. The coroutine infrastructure in `exclusive_callback.h` is framework-level code that doesn't depend on DF internals. Only the DF-specific key sequences fed through it need rewriting.

- **Is macOS supported?** Yes — DFHack releases include macOS builds, though documentation is outdated. Homebrew can supply build dependencies.

### Deferred to Implementation

- **Exact viewscreen types and focus strings for the new embark flow**: Need to run DF Steam with DFHack and inspect the actual viewscreen stack during embark. Cannot be determined from documentation alone.
- **Which df-structures headers have been renamed or removed**: Some `df/*.h` includes may have changed paths. Will discover during compilation — each missing header will produce a clear compiler error.
- **Work Details structure layout**: Deferred because MVP delegates to `autolabor`. If `autolabor` is broken in 53.x, inspect `df::work_detail` and related types to build direct management.
- **Farm plot crop selection API changes**: The plant/crop raw structures may have changed. Will verify against current df-structures during implementation.

---

## High-Level Technical Design

> *This illustrates the intended approach and is directional guidance for review, not implementation specification. The implementing agent should treat it as context, not code to reproduce.*

```
┌─────────────────────────────────────────────────┐
│                  df-ai plugin                    │
│                                                  │
│  plugin_init ──► register "ai" command           │
│  plugin_enable ──► create AI singleton           │
│  plugin_onupdate ──► EventManager.tick()         │
│                                                  │
│  ┌─────────┐  ┌──────────┐  ┌────────────────┐  │
│  │ Embark  │  │ AI Core  │  │ EventManager   │  │
│  │(UI sim) │  │          │  │ (callbacks)    │  │
│  └────┬────┘  └────┬─────┘  └───────┬────────┘  │
│       │            │                │            │
│       ▼            ▼                ▼            │
│  ┌─────────────────────────────────────────┐     │
│  │         Subsystem Managers              │     │
│  │                                         │     │
│  │  Plan          Stocks      Population   │     │
│  │  ├ dig         ├ farm      ├ citizens   │     │
│  │  ├ construct   ├ brew      ├ labors     │     │
│  │  ├ furnish     ├ hunt      ├ nobles     │     │
│  │  └ assign      ├ craft     └ (basic)    │     │
│  │                └ orders                 │     │
│  └──────────┬──────────────────────────────┘     │
│             │                                    │
│             ▼  (direct API, no UI simulation)    │
│  ┌─────────────────────────────────────────┐     │
│  │         DFHack API Layer                │     │
│  │  Buildings::  Maps::  Job::  Military:: │     │
│  │  world->manager_orders.all              │     │
│  │  plotinfo->  (was ui->)                 │     │
│  └─────────────────────────────────────────┘     │
└─────────────────────────────────────────────────┘
```

The key architectural shift: subsystem managers call DFHack APIs directly instead of routing through `ExclusiveCallback` for UI simulation. Only the Embark flow retains UI simulation (updated for DF Steam's new screens).

---

## Implementation Units

### Phase 1: Compilation & Build

- U1. **Set up DFHack build environment on macOS**

**Goal:** Clone DFHack 53.14-r2, install dependencies, verify the base DFHack builds, and integrate df-ai into the plugin tree.

**Requirements:** R1

**Dependencies:** None

**Files:**
- Modify: `CMakeLists.txt` (update minimum CMake version, remove 32-bit paths, verify Boost/SDL linkage)

**Approach:**
- Clone DFHack 53.14-r2 with `--recursive` to get df-structures
- Install macOS dependencies via Homebrew (cmake, ninja, gcc, boost, perl with XML::LibXML/LibXSLT)
- Symlink or copy `df-ai_develop/` into `dfhack/plugins/df-ai/`
- Add `add_subdirectory(df-ai)` to `dfhack/plugins/CMakeLists.txt`
- Verify DFHack itself builds before attempting df-ai compilation

**Patterns to follow:**
- DFHack build docs: https://docs.dfhack.org/en/stable/docs/dev/compile/Dependencies.html
- `plugins/examples/skeleton.cpp` for modern plugin structure reference

**Test scenarios:**
- Happy path: `ninja df-ai` produces a shared library without DFHack-level build errors

**Verification:**
- DFHack builds successfully on macOS
- df-ai is recognized as a plugin target (even if it doesn't compile yet)

---

- U2. **Mechanical API migration — global renames and include fixes**

**Goal:** Update all `REQUIRE_GLOBAL`, include paths, and global variable references to match DFHack 53.x naming.

**Requirements:** R1

**Dependencies:** U1

**Files:**
- Modify: `ai.cpp`, `camera.cpp`, `df-ai.cpp`, `event_manager.cpp`, `hooks.cpp`, `military.cpp`, `population.cpp`, `population_military.cpp`, `population_nobles.cpp`, `population_pets.cpp`, `plan_construct.cpp`, `room.cpp`, `stocks.cpp`, `stocks_find.cpp`, `stocks_forge.cpp`, `stocks_queue.cpp`, `stocks_update.cpp`, `stocks_equipment.cpp`, `trade_helpers.cpp`, `trade_manager.cpp`
- Modify: All files with `#include "df/ui.h"` or `#include "df/ui_sidebar_menus.h"`

**Approach:**
- `REQUIRE_GLOBAL(ui)` → `REQUIRE_GLOBAL(plotinfo)` across ~15 files
- `REQUIRE_GLOBAL(ui_sidebar_menus)` → `REQUIRE_GLOBAL(game)` in `plan_construct.cpp`, `population_pets.cpp`
- All `ui->` field accesses → `plotinfo->` (~50+ occurrences)
- `plotinfo->economic_stone[...]` in `plan_construct.cpp:50` and `stocks_find.cpp:797` — this field may be removed or restructured in DF Steam (economic stone designation was reworked). Find the replacement mechanism or stub these accesses
- `#include "df/ui.h"` → `#include "df/plotinfost.h"`
- `#include "df/ui_sidebar_menus.h"` → appropriate replacement header
- `ui_sidebar_mode` references: verify enum still exists or map to new equivalents
- `world->manager_orders` → `world->manager_orders.all` in all 7 files: `stocks_manager.cpp` (3 sites), `stocks_forge.cpp` (5 sites), `stocks_queue.cpp`, `stocks_update.cpp` (3 sites including `.erase()` — verify erase pattern is safe on `.all` vs compound type), `stocks_equipment.cpp` (3 sites), `stocks.cpp`, `population_pets.cpp`
- `Maps::GetBiomeType` → `Maps::getBiomeType` (if used)
- Remove `Buildings::containsTile` `room` parameter (if used)

**Patterns to follow:**
- DFHack 50.05-alpha1 dev changelog documents the global renames

**Test scenarios:**
- Happy path: All renamed symbols resolve correctly during compilation — no `undefined reference` or `undeclared identifier` errors for the renamed globals
- Edge case: `plotinfo->` field accesses that changed internal structure (e.g., `plotinfo->main.mode`) compile correctly against current df-structures

**Verification:**
- `grep -rn "REQUIRE_GLOBAL(ui)" *.cpp *.h` returns zero matches
- `grep -rn "world->manager_orders[^.]" *.cpp *.h` returns zero matches (all should be `world->manager_orders.all`)

---

- U3. **Stub non-MVP subsystems and fix remaining compilation errors**

**Goal:** Disable lockstep mode, CMV recording, weblegends, and advanced subsystems behind guards. Fix all remaining compilation errors from changed/removed df-structures types and viewscreen headers.

**Requirements:** R1

**Dependencies:** U2

**Files:**
- Modify: `hooks.cpp` (provide no-op implementations of `Hook_Update()`, `Hook_Shutdown()`, `Hook_Shutdown_Now()` — these are called from `df-ai.cpp` and `event_manager.cpp` unconditionally)
- Modify: `df-ai.cpp` (guard `#include "thirdparty/weblegends/weblegends-plugin.h"` and `add_weblegends_handler()` call behind `#ifdef DF_AI_WEBLEGENDS`)
- Modify: `event_manager.cpp` (guard `#include "thirdparty/dfplex/Client.hpp"`, `dfplex_client` member, and `is_client()` tick-level checks behind `#ifdef DF_AI_DFPLEX`)
- Modify: `weblegends.cpp` (exclude from build or stub exports to no-ops)
- Modify: `trade_manager.cpp`, `trade_helpers.cpp` (stub out ExclusiveCallback-based trade flow)
- Modify: `population_justice.cpp` (stub out justice system)
- Modify: `population_military.cpp` (stub to minimal — basic squad creation only)
- Modify: `plan_setup_screen.cpp` (fix `#include "../DataStaticsFields.cpp"` — fragile relative path into DFHack internals that may have moved)
- Modify: `CMakeLists.txt` (conditionally exclude stubbed files or add compile guards)
- Modify: Any files with broken `df/viewscreen_*.h` includes

**Approach:**
- `hooks.cpp` stubbing is not a simple `#ifdef` guard — three functions (`Hook_Update`, `Hook_Shutdown`, `Hook_Shutdown_Now`) are called unconditionally from `df-ai.cpp`, `event_manager.cpp`, and embark flow. Provide no-op stub implementations and set `lockstep_hooked = false`. The `df::renderer` subclass and SDL/tinythread includes can be guarded away since they reference vtable layouts that changed in 53.x
- Weblegends: `df-ai.cpp:14` includes `thirdparty/weblegends/weblegends-plugin.h` and `df-ai.cpp:125` calls `add_weblegends_handler()`. Guard both behind `#ifdef DF_AI_WEBLEGENDS`
- Dfplex: `event_manager.cpp` includes `thirdparty/dfplex/Client.hpp` and checks `is_client()` every tick in the update loop. Guard behind `#ifdef DF_AI_DFPLEX`
- Stub `Trade::update()` to return immediately
- Stub `Population::update_crimes()` and heavy military functions to no-ops
- For each remaining compilation error from df-structures changes:
  - Check if the type/field was renamed → update reference
  - Check if the type was removed → stub the using code
  - Check if the viewscreen type was removed → guard with `#ifdef` or replace
- Fix any `T_*` type promotions (anonymous types that became top-level)
- Remove the `std::make_unique` polyfill in `dfhack_shared.h` (C++14+ is standard now)
- Fix `plan_setup_screen.cpp` `#include "../DataStaticsFields.cpp"` — verify path in 53.x DFHack tree

**Test scenarios:**
- Happy path: `ninja df-ai` completes with zero errors and produces a shared library
- Edge case: Stubbed subsystems are genuinely inert — no runtime calls into unported code paths

**Verification:**
- Clean compilation with no errors or warnings-as-errors
- Shared library file exists at the expected install path

---

### Phase 2: Plugin Lifecycle & Core Loop

- U4. **Get plugin loading and AI singleton working**

**Goal:** The df-ai plugin loads in DFHack, registers the `ai` console command, and can be enabled/disabled without crashing.

**Requirements:** R2

**Dependencies:** U3

**Files:**
- Modify: `df-ai.cpp` (fix `plugin_init`, `plugin_enable`, `plugin_shutdown`, `plugin_onupdate`, `plugin_onstatechange`)
- Modify: `ai.cpp` (fix `AI::startup()`, `AI::update()`)
- Modify: `event_manager.cpp` (fix callback dispatch)
- Modify: `config.cpp` (fix config loading)

**Approach:**
- Install built plugin into DF Steam's `hack/plugins/` directory
- Launch DF Steam with DFHack, run `load df-ai` / `enable df-ai` from console
- Fix any runtime crashes from null globals, changed struct layouts, or missing viewscreen types
- Verify the `plugin_onupdate` → `EventManager::tick()` → subsystem update cycle runs without crashing
- The `check_enabled()` guard and `dwarfAI` singleton lifecycle should work as-is after the global renames
- Verify `plugin_onstatechange` handles world load/unload correctly

**Patterns to follow:**
- `plugins/examples/skeleton.cpp` for modern lifecycle patterns
- DFHack's `load` / `enable` / `disable` command flow

**Test scenarios:**
- Happy path: `enable df-ai` in DFHack console prints success message, `ai` command responds
- Error path: `enable df-ai` before a world is loaded does not crash (should gracefully defer)
- Happy path: Disabling and re-enabling works without memory leaks or double-free

**Verification:**
- Plugin loads, enables, and disables cleanly in DFHack console
- `ai report` produces output (even if subsystem data is empty/stubbed)
- No segfaults during enable/disable/world-load/world-unload cycle

---

- U5. **Port save/load persistence**

**Goal:** AI state persists across DF save/load cycles — room data, stock counts, and population state survive a save and reload.

**Requirements:** R9

**Dependencies:** U4

**Files:**
- Modify: `plan_persist.cpp` (fix persistence hooks for changed data structures)
- Modify: `ai.cpp` (fix `AI::persist()` / `AI::unpersist()` calls)
- Modify: `room.h` / `room.cpp` (fix room serialization if struct fields changed)

**Approach:**
- DFHack's persistence hooks (`plugin_save_world_data` / `plugin_load_world_data`) are the modern equivalents — verify df-ai's existing save/load hooks map to these
- Test by enabling AI, letting it run a few ticks, saving, reloading, and verifying state
- Fix any deserialization crashes from changed enum values or struct layouts

**Test scenarios:**
- Happy path: Enable AI → save → load → AI resumes where it left off
- Edge case: Loading a save that has no df-ai data does not crash

**Verification:**
- Round-trip save/load preserves room plan state and stock tracking data

---

### Phase 3: Fortress Layout & Construction

- U6. **Port the Plan system — blueprint generation and dig designation**

**Goal:** AI generates a fortress layout from JSON blueprints and designates tiles for digging.

**Requirements:** R4

**Dependencies:** U4

**Files:**
- Modify: `plan.cpp` (fix global refs, update any changed tile/map APIs)
- Modify: `plan_setup.cpp` / `plan_setup_blueprint.cpp` (fix blueprint generation)
- Modify: `plan_task.cpp` (fix dig designation — uses `Maps::getTileDesignation` directly)
- Modify: `plan_priorities.cpp` (fix priority scheduling)
- Modify: `blueprint.cpp` / `blueprint_*.cpp` (fix JSON blueprint parsing if needed)
- Verify: `plans/generic01.json` and `rooms/templates/` / `rooms/instances/` data files still valid

**Approach:**
- The dig designation system writes directly to tile designations via `Maps::getTileDesignation()` — this API is unchanged
- Blueprint loading from JSON (jsoncpp) should work as-is
- The room type enums and furniture type enums need validation against current df-structures
- Verify `building_type`, `workshop_type`, `furnace_type` enums haven't gained/lost/renumbered values
- New room types in DF Steam (if any) can be added later; existing templates should work for MVP

**Patterns to follow:**
- Existing `plan_task.cpp` dig designation pattern (direct tile modification)
- DFHack `quickfort` plugin for modern designation approaches

**Test scenarios:**
- Happy path: AI generates a blueprint, designates digging, dwarves begin mining out rooms
- Edge case: Blueprint placement handles Z-level transitions (stairs) correctly
- Edge case: Blocked dig designations (hard rock, aquifer) are detected and handled

**Verification:**
- Rooms appear as designated on the map
- Dwarves begin digging designated areas
- Room status transitions from Planned → Digging → (eventually) Dug

---

- U7. **Port the Construction system — workshops, furniture, and stockpiles**

**Goal:** AI builds workshops, furnaces, stockpiles, and places furniture in dug-out rooms.

**Requirements:** R5

**Dependencies:** U6

**Files:**
- Modify: `plan_construct.cpp` (fix building placement — already uses `Buildings::allocInstance` / `constructWithItems`)
- Modify: `plan.cpp` (fix stockpile creation and configuration)
- Modify: `room.h` (verify room/furniture enum mappings)

**Approach:**
- Building placement via `Buildings::allocInstance` + `constructWithItems/constructWithFilters` is unchanged — this is the path of least resistance
- Stockpile creation uses the same Buildings API with `building_type::Stockpile`
- Stockpile configuration (which items to accept): replace `ConstructStockpile` ExclusiveCallback (which navigated `viewscreen_layer_stockpilest`) with direct `building_stockpilest` field manipulation — verify these fields match current df-structures
- Activity zones (`building_civzonest`): replace `ConstructActivityZone` ExclusiveCallback (which navigated `dwarfmode/Zones` with `CIVZONE_*` keys) with `Buildings::allocInstance` + `Buildings::constructAbstract`. The old code accessed `ui_sidebar_menus->location.cursor_profession` and `.profession` for location assignment within zones (tavern, library, temple, guildhall) — this must be replaced with direct struct manipulation on the zone/location objects. Investigate `building_civzonest` field layout in current df-structures to understand how locations are assigned to zones programmatically
- Workshop and furnace types: verify `workshop_type` and `furnace_type` enums match current df-structures; add any new types relevant to MVP

**Patterns to follow:**
- Existing `plan_construct.cpp:535` pattern: `allocInstance` → `setSize` → `constructWithItems`
- DFHack `buildingplan` plugin for deferred material assignment

**Test scenarios:**
- Happy path: Workshop (carpenter, mason, craftsdwarf) is placed and becomes operational
- Happy path: Stockpile is created with correct item filters (food near kitchen, wood near carpenter)
- Happy path: Furniture (beds, chairs, tables, doors) is placed in bedrooms and dining hall
- Edge case: Construction with missing materials is deferred until materials are available
- Integration: Placed workshop becomes available for manager orders (U8)

**Verification:**
- Workshops, stockpiles, and furniture appear on the map as functional buildings
- Dwarves use placed furniture (sleep in beds, eat at tables)

---

### Phase 4: Survival — Food, Drink, and Labor

- U8. **Port the Stocks system — manager orders and production**

**Goal:** AI creates manager orders for essential production: meals, drinks, barrels, bins, furniture, and basic goods.

**Requirements:** R6, R7

**Dependencies:** U7

**Files:**
- Modify: `stocks.cpp` (fix main update loop)
- Modify: `stocks_queue.cpp` (replace ExclusiveCallback order creation with direct API)
- Modify: `stocks_manager.cpp` (replace UI-based order creation with `world->manager_orders.all.push_back`)
- Modify: `stocks_find.cpp` (fix item counting — verify item type enums)
- Modify: `stocks_update.cpp` (fix `world->manager_orders.all` iteration)
- Modify: `stocks_detect.cpp` (fix material detection)

**Approach:**
- The critical change: `stocks_manager.cpp` currently creates orders by navigating `viewscreen_jobmanagementst` and `viewscreen_createquotast` via ExclusiveCallback. Replace this entire flow with direct order creation:
  ```
  order = new df::manager_order()
  order->job_type = ...
  order->id = world->manager_orders.manager_order_next_id++
  world->manager_orders.all.push_back(order)
  ```
- Item counting in `stocks_find.cpp` should mostly work — `world->items` is still the source of truth
- Kitchen management (`stocks_update.cpp`) needs verification against current kitchen structure
- The item type enums in `stocks.h` (~120 types defined via `STOCKS_ENUMS`) need validation against current df-structures

**Patterns to follow:**
- DFHack `orders.cpp` plugin for programmatic order creation
- DFHack `autoslab.cpp` and `tailor.cpp` for modern manager order patterns

**Test scenarios:**
- Happy path: AI detects low food/drink stocks and creates brew/cook manager orders
- Happy path: AI orders barrel and bin production when storage is insufficient
- Happy path: AI orders furniture (beds, tables, chairs) as rooms are dug
- Edge case: Duplicate orders are not created (check existing orders before adding)
- Edge case: Orders with unavailable materials are handled gracefully
- Integration: Orders created via direct API appear in the in-game manager screen and are executed by dwarves

**Verification:**
- Manager orders appear in the DF manager screen
- Dwarves execute ordered jobs (brewing, cooking, carpentry)
- Food and drink stockpiles accumulate over time

---

- U9. **Port the Farm system — crop selection and farm plot management**

**Goal:** AI plants appropriate crops on farm plots by season and biome, producing food and brewing ingredients.

**Requirements:** R6

**Dependencies:** U7, U8

**Files:**
- Modify: `stocks_farm.cpp` (fix crop selection — verify plant raw structures)
- Modify: `plan_construct.cpp` (fix farm plot creation — `building_farmplotst`)

**Approach:**
- Farm plots are buildings (`building_farmplotst`) created via the Buildings API — same pattern as other buildings
- Crop selection reads `world->raws.plants` to find plantable species by biome and season — verify raw structure
- The crop assignment writes directly to `building_farmplotst::plant_id` per season — verify this field
- Plump helmets (underground, all-season) should be the default reliable crop
- Above-ground crops need verification against current biome/season plant data

**Patterns to follow:**
- DFHack `autofarm` plugin for modern crop management
- Existing `stocks_farm.cpp` crop selection logic

**Test scenarios:**
- Happy path: Farm plot is created and planted with appropriate underground crops
- Happy path: Crops rotate by season correctly
- Edge case: Farm plot in a biome with no valid crops falls back to plump helmets
- Integration: Harvested crops flow into food stockpile and brewing pipeline

**Verification:**
- Farm plots show planted crops
- Crops grow and are harvested
- Harvested plants are available for cooking/brewing

---

- U10. **Port basic Population management — Work Details and labor assignment**

**Goal:** AI assigns dwarves to appropriate Work Details so essential labors (mining, farming, brewing, carpentry, masonry, cooking) are covered.

**Requirements:** R7, R8

**Dependencies:** U4

**Files:**
- Modify: `population.cpp` (fix citizen tracking, enable `autolabor` delegation)
- Modify: `population_nobles.cpp` (replace `AssignNoblesExclusive` ExclusiveCallback with direct entity position assignment)
- Modify: `ai.cpp` (run `enable autolabor` during startup sequence)

**Approach:**
- **Labor management: delegate to `autolabor`**. df-ai already ran `enable autolabor` in 0.47. The modern `autolabor` was rewritten for DF Steam's Work Details system. Verify it works in 53.x by testing manually; if it does, this unit is simple — just ensure `autolabor` is enabled during AI startup. If `autolabor` is broken, fall back to direct `unit->status.labors[]` manipulation as an interim (per-unit flags may still function)
- **Noble assignment: replace `AssignNoblesExclusive`**. The old code navigated `viewscreen_layer_noblelistst` via ExclusiveCallback to assign noble positions. Replace with direct `historical_entity->positions.assignments` manipulation — iterate positions, find vacant ones (manager, broker, bookkeeper), and assign suitable citizens directly. Verify `entity_position_assignment` field layout in current df-structures
- Citizen tracking (`update_citizenlist`) should mostly work after the global renames — it reads `world->units` which is unchanged

**Patterns to follow:**
- DFHack `autolabor` / `labormanager` for modern labor management
- Existing `population_nobles.cpp` for noble position logic

**Test scenarios:**
- Happy path: Essential labors (mining, farming, carpentry, masonry, cooking, brewing) are covered by assigned dwarves
- Happy path: Noble positions (manager, broker, bookkeeper) are assigned to appropriate dwarves
- Edge case: New migrant wave triggers labor rebalancing
- Edge case: Fortress with only 7 starting dwarves covers all critical labors

**Verification:**
- Dwarves perform essential tasks without manual intervention
- Manager, broker, and bookkeeper positions are filled
- No critical labor goes permanently unassigned

---

- U11. **Port basic bedroom and dining hall assignment**

**Goal:** AI assigns bedrooms to dwarves and creates a functional dining hall so dwarves have shelter and a place to eat.

**Requirements:** R7

**Dependencies:** U6, U7, U10

**Files:**
- Modify: `plan_assign.cpp` (fix room assignment — bedroom, dining hall)
- Modify: `plan.cpp` (fix room ownership via `Buildings::setOwner` or direct struct manipulation)

**Approach:**
- Room assignment in 0.47 used furniture-based room definition (make a bed, define the room from it). DF Steam changed to a location-based system for some room types — verify whether bedrooms still use the furniture-based approach
- Dining hall assignment: verify whether the dining hall is still defined from a table/chair or if it's now a zone/location
- `Buildings::setOwner` or direct building owner assignment should work for bedrooms
- Meeting area (activity zone) for idle dwarves — verify `building_civzonest` + `Buildings::constructAbstract` pattern

**Test scenarios:**
- Happy path: Each dwarf gets a bedroom assigned after rooms are dug and furnished
- Happy path: Dining hall is functional and dwarves eat there
- Edge case: More dwarves than bedrooms — assignment prioritizes (or defers until more are dug)

**Verification:**
- Dwarves sleep in assigned bedrooms (no "slept without a proper room" thoughts)
- Dwarves eat in the dining hall

---

### Phase 5: Embark

- U12. **Port the Embark system for DF Steam's new UI**

**Goal:** AI can navigate from the title screen through world generation, site selection, and embark configuration to start a new fortress autonomously.

**Requirements:** R3

**Dependencies:** U4

**Files:**
- Modify: `embark.cpp` (rewrite ExclusiveCallback sequences for new UI)
- Modify: `embark.h` (update viewscreen type references)

**Approach:**
- This is the one subsystem that genuinely requires UI simulation — there's no direct API for world generation or embark
- The `ExclusiveCallback` coroutine framework is preserved; only the viewscreen types and key sequences change
- Step 1: Launch DF Steam with DFHack and manually walk through the embark flow, logging focus strings (`Gui::getFocusString`) and viewscreen types at each step
- Step 2: Map old viewscreen types to new ones:
  - `viewscreen_titlest` → verify still exists
  - `viewscreen_new_regionst` → verify or find replacement
  - `viewscreen_choose_start_sitest` → verify or find replacement
  - `viewscreen_setupdwarfgamest` → verify or find replacement
- Step 3: Rewrite each `ExpectScreen<T>` + key sequence in `embark.cpp` to match the new flow
- Step 4: The site finder algorithm (lat/lon scoring, biome preferences) should work once the UI navigation is fixed — the scoring logic doesn't depend on viewscreen types
- Consider adding a "skip embark" mode as a fallback — load an existing save and let the AI take over

**Execution note:** This unit requires iterative runtime testing — launch DF, try the sequence, observe what breaks, fix, repeat. Cannot be done by static analysis alone.

**Patterns to follow:**
- Existing `embark.cpp` ExclusiveCallback structure (preserve the coroutine pattern)
- DFHack focus string documentation for current viewscreen paths

**Test scenarios:**
- Happy path: AI navigates from title screen through world gen to a successful embark
- Edge case: World generation with non-default parameters (pocket world, etc.)
- Error path: Site finder finds no suitable site — AI handles gracefully (retries or accepts suboptimal site)
- Happy path: After embark, AI transitions to fortress management mode (U6-U11 kick in)

**Verification:**
- Starting from DF title screen, `enable df-ai` results in autonomous world gen → embark → fortress establishment
- Dwarves are on the map, digging begins, food production starts

---

### Phase 6: Integration & Polish

- U13. **End-to-end integration testing and crash fix sweep**

**Goal:** Run the full autonomous cycle (embark → dig → build → farm → survive) and fix all crashes, hangs, and logic errors discovered.

**Requirements:** R1–R9

**Dependencies:** U4–U12

**Files:**
- Modify: Any file where runtime issues are discovered

**Approach:**
- Run df-ai from a clean start: title screen → embark → let it run for 1-2 in-game years
- Monitor for: crashes, infinite loops, dwarves starving, rooms not being dug, workshops not being built, orders not being fulfilled
- Fix issues as they surface — this is inherently iterative
- Verify save/load mid-run
- Check DFHack console for error messages and warnings

**Test scenarios:**
- Happy path: AI embarks, digs a basic fortress, establishes food/drink production, dwarves survive 2 in-game years
- Edge case: First migrant wave is handled (new dwarves get bedrooms, labors assigned)
- Edge case: Save mid-run, reload, AI continues correctly
- Error path: Recover gracefully from a dwarf dying (reassign their labor, handle their room)

**Verification:**
- Fortress is self-sustaining after 2 in-game years
- No crashes during extended operation
- Population grows through migration and survives

---

## System-Wide Impact

- **Interaction graph:** The AI singleton (`dwarfAI`) drives all subsystems from `plugin_onupdate`. Subsystem updates (Plan, Stocks, Population) call DFHack APIs which modify DF world state. Callbacks in EventManager trigger cross-subsystem coordination (e.g., room completion triggers furnishing, which triggers manager orders for furniture).

- **Error propagation:** DFHack API calls that fail (null building, invalid position) should be caught and logged, not propagated as crashes. The old code has some null checks but many raw pointer dereferences — review critical paths during integration testing (U13).

- **State lifecycle risks:** The biggest risk is df-ai writing to a DF data structure whose layout changed between 0.47 and 53.x but wasn't caught at compile time (e.g., an int field that became an enum, or a vector that became a compound type). These cause silent corruption or delayed crashes. Mitigation: careful compile-time type checking and runtime assertions during integration testing.

- **API surface parity:** The `ai` console command, `ai report` output, and basic enable/disable flow should match the original plugin's interface as closely as possible for user familiarity.

- **Unchanged invariants:** The blueprint/room JSON format (`plans/`, `rooms/`) is not changing — existing blueprints should load without modification. The `dfhack-config/df-ai.json` configuration file format is not changing.

---

## Risks & Dependencies

| Risk | Mitigation |
|------|------------|
| df-structures headers renamed/removed beyond what documentation covers | Iterative compilation: fix each error as it appears. DFHack's df-structures has excellent grep-ability. |
| Viewscreen types for embark flow changed dramatically | Manual inspection of live DF Steam with DFHack before writing embark code. Accept that embark may need multiple debugging iterations. |
| Work Details API is underdocumented | Study DFHack's `autolabor` source. Fallback: delegate to `autolabor`/`labormanager` plugin. |
| macOS DFHack build issues (docs marked outdated) | Use Homebrew for all dependencies. If macOS builds fail, test on Linux as a fallback development platform. |
| DFHack version updates break df-ai | Pin to 53.14-r2 for MVP. Version bumps are separate maintenance work after MVP ships. |
| Some df-ai subsystems have deep coupling to old viewscreen types | Stubbing non-MVP subsystems isolates risk. MVP subsystems use direct APIs, not viewscreen navigation (except embark). |
| Building/stockpile internal field layouts changed silently | Compile-time type checking catches most issues. Runtime assertion on critical fields during integration testing. |
| Blueprint room types reference enum values that shifted | Validate all `building_type`, `workshop_type`, `furnace_type`, `stockpile_list` enum values against current df-structures before integration testing. |

---

## Phased Delivery

### Phase 1 — Compilation (U1–U3)
Get df-ai compiling against DFHack 53.14-r2. This is the foundation — everything else depends on it. Expected to be the most tedious phase (thousands of compile errors from renames and struct changes) but mechanically straightforward.

### Phase 2 — Core Loop (U4–U5)
Get the plugin loading and the AI singleton running its update cycle. Save/load working. This proves the plugin infrastructure is viable before investing in subsystem ports.

### Phase 3 — Fortress Layout (U6–U7)
Blueprint generation, dig designation, and construction. This is the "visible progress" milestone — rooms appear on the map, workshops get built, furniture gets placed.

### Phase 4 — Survival (U8–U11)
Food production, labor management, and room assignment. This is the "dwarves stay alive" milestone — the fortress becomes self-sustaining.

### Phase 5 — Embark (U12)
Automated world gen and embark. This is the "fully autonomous" milestone — the AI can start from nothing.

### Phase 6 — Integration (U13)
End-to-end testing and crash fixes. This is the "it actually works" milestone.

---

## Sources & References

- DFHack 53.14-r2 documentation: https://docs.dfhack.org/en/stable/
- DFHack GitHub: https://github.com/DFHack/dfhack
- df-structures: https://github.com/DFHack/df-structures
- Original df-ai by BenLubar: https://github.com/BenLubar/df-ai
- DFHack dev changelog (breaking changes): https://docs.dfhack.org/en/stable/docs/NEWS-dev.html
- DFHack build guide: https://docs.dfhack.org/en/stable/docs/dev/compile/Compile.html
- DFHack plugin development intro: https://docs.dfhack.org/en/stable/docs/dev/Dev-intro.html
- Dwarf Fortress Steam release notes: https://store.steampowered.com/news/app/975370
