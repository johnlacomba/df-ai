---
title: "feat: Add Healthcare and Military Support to df-ai"
type: feat
status: active
date: 2026-05-30
---

# feat: Add Healthcare and Military Support to df-ai

## Overview

Restore and implement proper healthcare and military subsystems for df-ai on Steam DF (v53.14 / DFHack 53.14-r2). The old military code (1301 lines) used UI automation via `viewscreen_layer_militaryst` which no longer exists. The new approach uses direct struct manipulation of `df::squad`, `squad_position`, `squad_schedule_entry`, and related structures that still exist in the current DFHack XML definitions. Healthcare follows the existing location pattern (tavern/library/temple) using `abstract_building_hospitalst`.

---

## Problem Frame

df-ai currently has:
- **Military**: Only threat detection (`tag_enemies`). Squad attack orders, training, equipment, barracks assignment, and alert management are all stubbed. Dwarves don't get drafted, equipped, or trained.
- **Healthcare**: Chief Medical Dwarf appointment works. Hospital zone creation is deferred. No supply management, no doctor labor assignment beyond CMD, no traction benches.

Without these systems, the fortress is defenseless (no trained soldiers) and dwarves with injuries die from lack of treatment.

---

## Requirements Trace

- R1. Create and manage military squads with configurable population percentage
- R2. Assign equipment/uniforms to squad members (melee and ranged)
- R3. Set up barracks with training schedules
- R4. Manage alert levels (training routine vs active duty)
- R5. Disable conflicting civilian labors for soldiers (Mining, Woodcutting, Hunting)
- R6. Create hospital zones using the location system
- R7. Appoint and configure Chief Medical Dwarf (already partially working)
- R8. Assign all 5 doctoring labors to appropriate dwarves
- R9. Maintain hospital supply stocks (thread, cloth, splints, crutches, soap)
- R10. Build traction benches for compound fracture treatment

---

## Scope Boundaries

- No offensive raid/mission system (future work)
- No complex military tactics or formations
- No custom per-dwarf equipment overrides (squad-level uniforms only)
- No training weapon vs combat weapon distinction initially
- No archery target management
- No artifact display or strange mood handling
- No stress management (separate feature)

### Deferred to Follow-Up Work

- Marksdwarf ammunition management (known v50 bugs make this unreliable)
- Multi-alert-level scheduling (start with single training routine)
- Mercenary resident recruitment into military
- Hospital supply quality tiers

---

## Context & Research

### Relevant Code and Patterns

- `population_nobles.cpp:181-208` — Working CMD assignment via `entity_position_responsibility::HEALTH_MANAGEMENT`. Model for role assignment.
- `plan_construct.cpp:1494-1540` — Location construction pattern (tavern/library/temple/guildhall). Hospital should follow this exactly, adding `abstract_building_hospitalst`.
- `population_occupations.cpp:91-187` — Location-based role management. Model for assigning hospital staff.
- `plan_assign.cpp:227-290` — Existing `getsoldierbarrack()` and stubbed `assign_barrack_squad()`.
- `military.cpp:22-119` — Working `tag_enemies()` with threat detection and dispatch.
- Git history (`10c52e7:population_military.cpp`) — Full old implementation (1301 lines) showing draft/dismiss logic, population percentage calculation, noble exclusion, tool confiscation.

### DFHack Struct Availability (Confirmed in df.squad.xml)

| Structure | Status | Purpose |
|-----------|--------|---------|
| `df::squad` | Available | Squad container with positions, orders, schedule, rooms |
| `squad_position` | Available | Per-member slot with equipment and barracks prefs |
| `squad_position_equipmentst` | Available | Uniform definition per position |
| `squad_uniform_spec` | Available | Item type + material for equipment slots |
| `squad_schedule_entry` | Available | Monthly schedule with sleep/uniform mode |
| `squad_schedulest` | Available | Full schedule container |
| `squad_barracks_infost` | Available | Barracks-to-squad assignment with use flags |
| `squad_use_flags` | Available | sleep/train/equip/squad_eq flags for barracks |
| `abstract_building_hospitalst` | Available | Hospital location type |
| `unit->status.labors[]` | Available | Direct labor manipulation |
| `unit->military.squad_id` | Available | Squad membership |
| `unit->military.squad_position` | Available | Position within squad |

### Key Differences from Old Code

| Aspect | Old (Classic DF) | New (Steam DF) |
|--------|------------------|----------------|
| Squad management | UI automation via viewscreen_layer_militaryst | Direct struct manipulation |
| Alert levels | `squad->cur_alert_idx` (simple index) | `squad->cur_routine_idx` into `squad->schedule` |
| Barracks assignment | `building_squad_use` on building | `squad->rooms` vector of `squad_barracks_infost` |
| Equipment | UI-driven entity_uniform creation | Direct `squad_position->equipment` manipulation |
| Hospital | Civzone-based | Location-based (abstract_building_hospitalst on meeting area) |

### External References

- [Military - Dwarf Fortress Wiki](https://dwarffortresswiki.org/index.php/Military) — Squad structure, training, equipment, barracks, known bugs
- [Health care - Dwarf Fortress Wiki](https://dwarffortresswiki.org/index.php/Health_care) — 5 doctoring labors, hospital setup, supply needs
- [Farming - Dwarf Fortress Wiki](https://dwarffortresswiki.org/index.php/Farming) — Plaster powder source (for casts)

---

## Key Technical Decisions

- **Direct struct manipulation over UI automation**: The old ExclusiveCallback-based UI navigation is impossible (viewscreen removed). All squad/schedule/equipment changes will be done by directly writing to DFHack structures. This is actually simpler and more reliable than the old approach.
- **Hospital as location_type**: Add `hospital` to the `location_type` enum in `room.h` and follow the tavern/library/temple construction pattern using `abstract_building_hospitalst`.
- **Autolabor integration for doctor labors**: df-ai already uses autolabor/labormanager for most labor management. Doctor labors should be managed similarly — enable on qualified citizens, let autolabor handle the rest. CMD gets all 5 labors directly (existing pattern).
- **Population-percentage military**: Restore the old `military_min`/`military_max` config (already declared in `population.h`) for target military size.
- **Squad-level uniforms**: Equipment is assigned per-squad (all members get same uniform), not per-dwarf. Simpler and matches how most players use it.

---

## Open Questions

### Resolved During Planning

- **How to assign barracks to squads?**: Use `squad->rooms` vector — push a `squad_barracks_infost` with the barracks building_id and appropriate `squad_use_flags` (sleep, train, equip).
- **How to set training schedule?**: Set entries in `squad->schedule` with the training routine. The `cur_routine_idx` field selects which schedule is active.
- **Where does hospital location come from?**: `abstract_building_hospitalst` instantiated and linked to site, same pattern as tavern/library/temple in `plan_construct.cpp`.

### Deferred to Implementation

- **Exact schedule entry format**: Need to inspect a running game's squad schedule to verify how `squad_schedule_entry` fields map to "train X months, active Y months". Will discover during implementation.
- **squad_position.occupant assignment**: Whether setting `occupant` on squad_position is sufficient to draft a dwarf, or if `unit->military.squad_id` must also be set manually. The old code set both via UI; direct manipulation may need both.
- **Hospital supply stockpile linking**: Whether the hospital location auto-pulls supplies or needs a linked stockpile. May need to check in-game behavior.

---

## Implementation Units

- U1. **Hospital Location Type and Zone Construction**

**Goal:** Enable df-ai to create hospital zones using the location system, following the existing tavern/library/temple pattern.

**Requirements:** R6

**Dependencies:** None

**Files:**
- Modify: `room.h` (add `hospital` to location_type enum)
- Modify: `plan_construct.cpp` (add hospital case to activity zone construction, remove infirmary deferral)
- Modify: `plans/generic01.json` (add hospital room definition or repurpose infirmary)
- Modify: `plan_persist.cpp` (add hospital to enum names if needed)

**Approach:**
- Add `ENUM_ITEM(hospital)` to the `location_type` enum in `room.h`
- In `try_construct_activityzone`, add `case location_type::hospital:` that instantiates `abstract_building_hospitalst` (identical pattern to tavern/library/temple)
- Remove the "Infirmary zone deferred" early return in `plan_construct.cpp:1462-1465`
- Either repurpose the existing `room_type::infirmary` with `location_type::hospital`, or convert infirmary rooms to location rooms with hospital subtype in the blueprint

**Patterns to follow:**
- `plan_construct.cpp:1494-1540` — Existing location construction (tavern case is the model)
- `plan_construct.cpp` ConstructActivityZoneExclusive class — Zone creation with abstract_building linkage

**Test scenarios:**
- Happy path: Hospital room transitions from dug -> constructed with abstract_building_hospitalst created and linked to site
- Happy path: Hospital appears in fortress location list after construction
- Edge case: Multiple hospitals can be created (generic01.json allows 1-5 infirmaries)
- Error path: Hospital construction gracefully handles missing site (null site check)

**Verification:**
- Hospital zones appear in-game as functional locations
- Dwarves can be treated at the hospital location
- No crash or assertion when constructing hospital

---

- U2. **Healthcare Labor and Supply Management**

**Goal:** Ensure sufficient dwarves have doctoring labors enabled and hospital supplies are maintained.

**Requirements:** R7, R8, R9, R10

**Dependencies:** U1

**Files:**
- Modify: `population_nobles.cpp` (extend CMD logic to assign additional doctors)
- Modify: `stocks.h` (add hospital supply stock items if not present)
- Modify: `stocks_queue.cpp` (add manager orders for hospital supplies)
- Modify: `plans/generic01.json` (ensure traction bench in hospital layout)

**Approach:**
- **Doctor labor assignment**: Beyond CMD (who already gets all 5 labors), enable individual doctoring labors on additional citizens. Target: at least 2 dwarves with DIAGNOSE, 2 with SURGERY, 2 with BONE_SETTING, 2 with SUTURING, 2 with DRESSING_WOUNDS. Use `unit->status.labors[unit_labor::X] = true` directly.
- **Hospital supplies**: Add stock tracking for thread, cloth, splints, crutches, soap, plaster powder. Queue manager orders when supplies fall below threshold (e.g., 10 thread, 5 cloth, 5 splints, 5 crutches, 5 soap bars).
- **Traction bench**: Add traction bench to hospital room layout in blueprint. It's a furniture item built at craftsdwarf's workshop (wood) or metalsmith's forge (metal).
- **Supply integration with autolabor**: Doctor labors should NOT be managed by autolabor (they're specialized). Directly enable them on citizens who aren't in military or holding noble positions.

**Patterns to follow:**
- `population_nobles.cpp:181-208` — Existing CMD labor assignment pattern
- `stocks_queue.cpp` — Manager order queuing for production needs
- `plans/generic01.json` — Room layout definitions with furniture items

**Test scenarios:**
- Happy path: CMD gets all 5 doctoring labors assigned automatically
- Happy path: Additional citizens get individual doctoring labors when population > 15
- Happy path: Manager orders queued for thread/cloth/splints/crutches/soap when stock < threshold
- Edge case: Don't assign doctor labors to military members or nobles
- Edge case: If only 7 dwarves, assign labors to fewer doctors (scale with population)
- Integration: Traction bench appears in hospital layout and is built by craftsdwarf

**Verification:**
- Multiple dwarves have doctoring labors enabled
- Hospital supplies are maintained above minimum thresholds
- Injured dwarves receive treatment (diagnosis -> surgery/suturing/bone setting -> wound dressing)

---

- U3. **Squad Creation and Drafting**

**Goal:** Create military squads and draft appropriate citizens based on population percentage targets.

**Requirements:** R1, R5

**Dependencies:** None (parallel with U1/U2)

**Files:**
- Modify: `population_military.cpp` (implement `update_military`, squad creation, drafting)
- Modify: `population.h` (add any needed helper declarations)
- Modify: `plan_assign.cpp` (implement `assign_barrack_squad` stub)

**Approach:**
- **Squad creation**: When military population is below `military_min` percentage and no squads exist, create a squad:
  1. Allocate `df::squad` via `df::allocate<df::squad>()`
  2. Set `entity_id` to fortress entity
  3. Create 10 `squad_position` entries (standard squad size)
  4. Link to fortress entity's squad list
  5. Insert into `world->squads.all`
- **Drafting logic** (restored from old code):
  1. Calculate target military size: `citizen.size() * military_min / 100` to `military_max / 100`
  2. Build draft pool: adult citizens not in moods, not nobles (broker/manager/bookkeeper), not miners/woodcutters/hunters
  3. Draft by assigning `unit->military.squad_id` and `unit->military.squad_position`, plus setting `squad_position->occupant` to unit's `hist_figure_id`
- **Labor conflict resolution**: When a citizen is drafted, disable Mining, Woodcutting, and Hunting labors. When dismissed, re-enable them (let autolabor handle).
- **Noble exclusion**: Never draft dwarves with ACCOUNTING, MANAGE_PRODUCTION, or TRADE responsibilities (restored from old code).

**Patterns to follow:**
- Git history `10c52e7:population_military.cpp:657-903` — Old `update_military()` logic for draft pool selection, percentage calculation, noble exclusion
- `population_nobles.cpp` — Pattern for iterating citizens and checking conditions

**Test scenarios:**
- Happy path: Squad created when military_min > 0 and no squads exist
- Happy path: Citizens drafted up to military_min percentage of population
- Happy path: Mining/Woodcutting/Hunting labors disabled on drafted dwarves
- Edge case: Nobles (broker, manager, bookkeeper) excluded from draft pool
- Edge case: Children and babies excluded from draft pool
- Edge case: Dwarves in strange moods excluded from draft pool
- Error path: If all eligible citizens are excluded, no crash (empty draft pool handled)
- Integration: Drafted dwarves appear in squad roster in-game

**Verification:**
- Squads are created and populated with soldiers
- Military population tracks configured percentage
- Soldiers don't have conflicting civilian labors

---

- U4. **Equipment and Uniform Assignment**

**Goal:** Assign basic equipment (weapons + armor) to squad members.

**Requirements:** R2

**Dependencies:** U3

**Files:**
- Modify: `population_military.cpp` (add equipment assignment logic)
- Possibly modify: `stocks.h` / `stocks_queue.cpp` (ensure weapon/armor production)

**Approach:**
- **Uniform definition**: Set `squad_position->equipment` for each position in the squad:
  - Melee squad (default): metal weapon (sword/axe/mace), metal armor (mail shirt), metal helm, metal shield, metal gauntlets, metal high boots
  - Ranged squad (first squad): crossbow, metal armor, metal helm, bolts
- **Material specification**: Use `squad_uniform_spec` with `material_class = entity_material_category::Armor` for armor pieces, appropriate category for weapons
- **Alternate approach if direct struct manipulation is insufficient**: Fall back to using `entity_uniform` on the fortress entity and linking squads to those uniforms via `squad->uniform_priority` or position equipment references
- **Stock integration**: Ensure stocks system queues metalsmith orders for needed equipment (may already happen via existing manager order logic)

**Patterns to follow:**
- `df.squad.xml:162-176` — `squad_position_equipmentst` structure with `uniform` array indexed by `uniform_category`
- `df.squad.xml:146-160` — `squad_uniform_spec` with item_type, material_class, indiv_choice
- Git history `10c52e7:population_military.cpp:353-355` — Old `check_uniform_item` lambda showing expected structure

**Test scenarios:**
- Happy path: All squad positions have equipment assigned after squad creation
- Happy path: Soldiers equip assigned items when available in stockpile
- Edge case: If no metal armor available, soldiers still function (just unequipped)
- Edge case: Equipment specs don't crash if item types are invalid for current entity

**Verification:**
- Squad equipment screen shows assigned uniform items
- Soldiers pick up and wear assigned equipment
- No crashes when inspecting squad equipment

---

- U5. **Barracks Assignment and Training Schedule**

**Goal:** Link squads to barracks rooms and set up a training schedule so soldiers actually train.

**Requirements:** R3, R4

**Dependencies:** U3

**Files:**
- Modify: `population_military.cpp` (add barracks monitoring and schedule setup)
- Modify: `plan_assign.cpp` (implement `assign_barrack_squad()`)

**Approach:**
- **Barracks-squad linking**: When a barracks room is dug and furnished:
  1. Create `squad_barracks_infost` with the barracks building_id
  2. Set `mode` flags: `sleep = true`, `train = true`, `equip = true`
  3. Push to `squad->rooms` vector
  4. Also set each `squad_position->preferences[barrack_preference_category::Bed]` to the barracks building_id
- **Training schedule**: Set `squad->schedule` entries to create a "train all months" routine:
  1. For each month in the schedule, create a `squad_schedule_entry` with training orders
  2. Set `squad->cur_routine_idx` to the training routine index
  3. Reference: `squad_schedule_entry` has `sleep_mode` (InBarracksAtWill) and `uniform_mode` (Regular)
- **Restore old barracks monitoring**: From old code — check barracks construction status, and when ready, activate training by setting the routine

**Patterns to follow:**
- `df.squad.xml:250-253` — `squad_barracks_infost` with building ref and `squad_use_flags`
- `df.squad.xml:224-233` — `squad_schedule_entry` with orders, sleep_mode, uniform_mode
- Git history `10c52e7:population_military.cpp:862-903` — Old barracks monitoring and `cur_alert_idx` setting
- `plan_assign.cpp:227-251` — Existing `getsoldierbarrack()` room allocation

**Test scenarios:**
- Happy path: Squad linked to barracks after barracks is built and furnished
- Happy path: Training schedule set — soldiers begin training in barracks
- Happy path: Soldiers sleep in barracks when assigned
- Edge case: Multiple squads can share a barracks (or get separate ones)
- Edge case: If barracks building isn't built yet, skip assignment (check build stage)
- Error path: Gracefully handle case where barracks room has no furniture building

**Verification:**
- Squads show as assigned to barracks in-game
- Soldiers report to barracks for training
- Military screen shows active training schedule

---

- U6. **Squad Attack Order Dispatch**

**Goal:** Implement the stubbed attack order functions so `tag_enemies` can actually dispatch squads against threats.

**Requirements:** R1

**Dependencies:** U3

**Files:**
- Modify: `population_military.cpp` (implement attack order functions)

**Approach:**
- **`military_squad_attack_unit`**: Create `df::squad_order_kill_listst`, add target unit_id to its `units` vector, push to `squad->orders`.
- **`military_random_squad_attack_unit`**: Pick a random squad from `plotinfo->main.fortress_entity->squads`, call `military_squad_attack_unit`.
- **`military_all_squads_attack_unit`**: Iterate all squads, call `military_squad_attack_unit` for each.
- **`military_cancel_attack_order`**: Find and remove kill orders targeting the given unit from squad order lists.
- These are straightforward because `tag_enemies` already does the threat classification — we just need the dispatch to actually create orders.

**Patterns to follow:**
- `military.cpp:30-50` — Existing code that reads `squad->orders` and `squad_order_kill_listst->units` (reverse of what we're writing)
- Git history `10c52e7:population_military.cpp:1189-1300` — Old implementation of these functions (via UI, but shows the data flow)

**Test scenarios:**
- Happy path: When megabeast detected, all squads receive kill order targeting it
- Happy path: When invader detected, random squad receives kill order
- Happy path: When target leaves map, kill order is cancelled
- Edge case: Don't issue duplicate kill orders for same target
- Edge case: Handle empty squad list gracefully (no squads yet)
- Error path: If squad has no living members, skip it

**Verification:**
- Soldiers actually move to engage threats when detected
- Kill orders appear in squad order list in-game
- Orders are properly cancelled when threats leave

---

## System-Wide Impact

- **Interaction graph:** `tag_enemies` (military.cpp) calls the attack dispatch functions. `update_military` (population_military.cpp) manages squad composition. Plan system creates barracks/hospital rooms. Stocks system needs to ensure equipment and medical supplies are produced.
- **Error propagation:** Null squad/unit pointers must be guarded throughout (lesson from the tag_enemies crash). All new code should null-check any `::find()` result.
- **State lifecycle risks:** Drafting a dwarf changes their labor set — if autolabor runs concurrently, it could re-enable conflicting labors. May need to mark military dwarves specially for autolabor.
- **API surface parity:** Hospital location construction must mirror tavern/library/temple exactly to avoid the constructAbstract vs constructWithItems issue from farmplots.
- **Integration coverage:** The military draft -> labor disable -> autolabor interaction needs verification. Hospital zone creation -> location listing -> dwarf treatment chain needs end-to-end testing.

---

## Risks & Dependencies

| Risk | Mitigation |
|------|------------|
| Direct squad struct manipulation may not be recognized by game engine (squad exists but game doesn't "see" it) | Verify by checking if soldiers report to barracks after struct changes. If not, investigate what additional game state needs updating (e.g., activity creation, announcement triggers) |
| Autolabor re-enables Mining/Woodcutting on drafted soldiers | Investigate autolabor exclusion mechanism. May need to use autolabor's own "exclude from management" flag or disable the specific labors every update cycle |
| Hospital supply production conflicts with food/drink priority | Set appropriate manager order priorities. Medical supplies are low-volume (5-10 items each), won't conflict with food production |
| Squad schedule struct may have undocumented requirements | Inspect a manually-created squad's schedule struct in a running game to verify field values before implementing. Defer to implementation phase |
| Equipment assignment may require entity_uniform creation first | If direct position equipment doesn't work, fall back to creating entity uniforms on the fortress entity and linking them. The old code shows this path |

---

## Sources & References

- Related code: `population_military.cpp`, `military.cpp`, `population_nobles.cpp`, `plan_construct.cpp`, `plan_assign.cpp`
- Git history: `10c52e7:population_military.cpp` (full old military implementation, 1301 lines)
- DFHack XML: `df.squad.xml` (squad struct definitions confirmed available)
- Wiki: [Military](https://dwarffortresswiki.org/index.php/Military), [Health care](https://dwarffortresswiki.org/index.php/Health_care)
