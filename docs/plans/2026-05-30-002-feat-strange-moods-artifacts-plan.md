---
title: "Strange Moods, Artifact Handling, and Museums"
status: active
created: 2026-05-30
scope: df-ai strange mood support, artifact display, museum location
dependencies:
  - 2026-05-30-001-feat-healthcare-military-support-plan.md (hospital zone pattern reused for museum)
---

# Strange Moods, Artifact Handling, and Museums

## Problem Frame

df-ai currently has zero mood handling. When a dwarf enters a strange mood, the AI does nothing — it doesn't detect the event, doesn't ensure materials are accessible, and doesn't manage the aftermath. Moody dwarves can go insane from lack of materials or workshop access while df-ai is oblivious.

Similarly, artifacts produced by moods just sit in stockpiles. Steam DF (v50+) introduced pedestals, display cases, and a museum/gallery location system that df-ai doesn't use.

## Scope

### In Scope
- Strange mood detection and logging
- Material accessibility checks (unforbid relevant materials, ensure stockpile accessibility)
- Workshop availability verification (moody dwarf claims a workshop; ensure one exists)
- Display furniture: pedestals and display cases for artifacts
- Museum location: a late-game gallery zone with pedestals for artifact display
- Artifact tracking and assignment to display furniture

### Out of Scope
- Mood manipulation (forcing moods, choosing mood type)
- Artifact trading decisions (already handled in trade_helpers.cpp — artifacts excluded from trade)
- Strange mood material *production* (df-ai's existing stocks system already maintains bone, shell, gem, cloth, leather, etc.)
- Fell/macabre mood special handling (these just produce artifacts from a different mood type; same workshop/material logic applies)

## Key Decisions

1. **Mood material supply is passive, not active**: df-ai already maintains stock minimums for bone (2), rough_gem, leather, cloth, shell, wood, stone, block. A moody dwarf's needs are met by existing stocks logic. The mood handler only needs to *unforbid* relevant materials if they got forbidden (e.g., from combat) and ensure workshops aren't profile-locked.

2. **Museum is a late-game feature**: Gated behind citizen count (50+). Before that, artifacts go on individual pedestals in high-traffic areas.

3. **Display cases don't exist in df-ai's item system**: Need to add `stock_item::display_case` and the corresponding `layout_type::display_case` → `building_type::DisplayFurniture` mapping (display cases use the same building type as pedestals but are a different tool_uses item).

4. **Museum uses the same location pattern as hospital/library**: MeetingHall zone + abstract_building on site. The abstract building type for museums in Steam DF is `abstract_building_museumst` (if it exists) or we use the gallery/display approach if the game structure differs.

5. **No active mood intervention**: df-ai won't try to move items to workshops or build emergency workshops. The game handles mood item gathering. df-ai just ensures the preconditions are met (workshop exists, materials not forbidden).

## Implementation Units

### U1: Strange Mood Detection and Monitoring

**Goal**: Detect when a citizen enters a strange mood, log it, track the mood state, and perform basic accessibility checks.

**Files**:
- Create: `population_moods.cpp`
- Modify: `population.h` (add mood tracking members and `update_moods` declaration)
- Modify: `population.cpp` (add `update_moods` call in update cycle, case 9 which currently only does JSON reporting)
- Modify: `CMakeLists.txt` (add new .cpp)

**Approach**:
- Add `std::map<int32_t, df::mood_type> moody` member to Population tracking citizens currently in moods
- `update_moods()` iterates `world->units.active`, checks `Units::isCitizen(u) && u->mood != mood_type::None`
- On new mood detection: log the mood type and unit, check that the claimed workshop (if any) is accessible
- On mood completion (unit was in `moody` map but now `mood == None` and has `artifact` flag): log artifact creation
- On mood failure (unit goes insane — `mood == Berserk/Melancholy/Catatonic` after being in moody map): log failure
- Unforbid materials: iterate items in the workshop's building zone, unforbid anything forbidden

**Patterns to follow**: `population_nobles.cpp` structure (separate .cpp file for a population subsystem), `update_military`/`update_nobles` pattern

**Verification**: Compiles, mood detection fires in log when a dwarf gets a mood, completion logged when artifact created

### U2: Material Accessibility

**Goal**: When a mood is detected, ensure common mood materials aren't forbidden and relevant stockpiles are accessible.

**Files**:
- Modify: `population_moods.cpp` (add material check logic within mood detection)
- Modify: `stocks.cpp` (bump minimums for mood-critical raw materials)

**Approach**:
- When a new mood is detected, scan items in the fortress for `flags.bits.forbid` on raw materials (bones, shells, gems, cloth, leather, wood logs, stone, metal bars, thread)
- Unforbid any forbidden mood-relevant materials (combat debris often gets mass-forbidden)
- Increase stock minimums: `rough_gem` from current to 5, `bone` from 2 to 5, `shell` from current to 3, `leather` from current to 5
- The existing stocks queue system will naturally produce/acquire these materials over time

**Patterns to follow**: `stocks.cpp` Needed[] initialization pattern

**Verification**: After a mood fires, forbidden bones/gems/etc. get unforbidden automatically

### U3: Display Furniture (Pedestals and Display Cases)

**Goal**: Add display case support alongside existing pedestals, increase pedestal/display case production for artifact display.

**Files**:
- Modify: `stocks.h` (add `display_case` to stock_item enum if not using pedestal for both)
- Modify: `stocks.cpp` (increase pedestal needed count based on artifact count)
- Modify: `stocks_find.cpp` (display case find logic if separate from pedestal)
- Modify: `stocks_queue.cpp` (display case queue logic if separate)
- Modify: `room.h` (add `display_case` to layout_type if needed)
- Modify: `plan_construct.cpp` (map display_case to DisplayFurniture)

**Approach**:
- In Steam DF, both pedestals and display cases are `building_type::DisplayFurniture`. They differ by the tool_uses on the item definition (DISPLAY_OBJECT for pedestals). Display cases may use a different tool_uses or item subtype.
- Increase `Needed[stock_item::pedestal]` from 1 to `max(2, artifact_count + 1)` where artifact_count is tracked
- If display cases are a separate item type in Steam DF, add them; otherwise just use more pedestals
- Place pedestals in noble rooms and high-traffic locations (dining hall)

**Verification**: Multiple pedestals/display cases get built, artifact count drives demand

### U4: Artifact Tracking and Display Assignment

**Goal**: Track artifacts produced by the fortress and assign them to display furniture.

**Files**:
- Create or modify: `population_moods.cpp` (artifact tracking and display assignment)
- Modify: `population.h` (add artifact tracking member)

**Approach**:
- Maintain `std::set<int32_t> artifacts` tracking item IDs of fortress-produced artifacts
- On mood completion, add the produced artifact's item ID to the set
- Also scan `world->artifacts.all` periodically for any artifacts owned by the fortress
- For each unassigned artifact (not on a pedestal/display), find an empty DisplayFurniture building and assign the artifact to it using the building's `displayed_items` vector
- Assignment uses the same mechanism the game uses: add item reference to building, set item's `general_refs` to point at the building

**Verification**: Artifacts get placed on pedestals automatically after mood completion

### U5: Museum Location (Late-Game)

**Goal**: Create a museum/gallery location when the fortress is large enough, furnish it with pedestals for artifact display.

**Files**:
- Modify: `room.h` (add `museum` to location_type enum)
- Modify: `schemas/enums.json` (add "museum" to location_type)
- Create: `rooms/instances/generic01_location/museum.json` (room instance)
- Modify: `plan_construct.cpp` (museum zone construction, like hospital pattern)
- Modify: `population_moods.cpp` or `population_occupations.cpp` (museum creation trigger)

**Approach**:
- Gate behind `citizen.size() >= 50`
- Museum follows the hospital pattern: MeetingHall civzone + abstract_building linked to site
- The abstract building type depends on what Steam DF exposes (likely `abstract_building_museumst` or similar)
- Room layout: 10x10 zone with pedestals arranged in grid (6-8 pedestals)
- Prefer placing artifacts in museum pedestals over scattered pedestals
- If `abstract_building_museumst` doesn't exist in DFHack headers, defer this unit until the structure is identified

**Verification**: Museum zone created after 50 citizens, pedestals placed, artifacts preferentially displayed there

## Dependencies

- U1 → U2 (material checks happen on mood detection)
- U1 → U4 (artifact tracking starts from mood completion)
- U3 → U4 (need display furniture before assigning artifacts)
- U3 + U4 → U5 (museum is the culmination of display infrastructure)

## Risks

1. **Museum abstract_building type may not exist in DFHack 53.14-r2 headers**: Museums/galleries are a v50 feature but DFHack struct coverage lags. If the type isn't available, U5 gets deferred. Mitigation: check headers during implementation; worst case we just place more standalone pedestals.

2. **Display assignment API uncertainty**: How items get "assigned" to display furniture in Steam DF's internal structs isn't fully documented. May need to examine save files or DFHack's own display handling. Mitigation: look at how DFHack's `buildingplan` or `assign-items` commands work.

3. **Mood material unforbidding could be too aggressive**: Mass-unforbidding might expose dangerous items (forgotten beast extract, etc.). Mitigation: only unforbid specific material categories relevant to crafting moods, not everything.

## Test Scenarios

- Dwarf enters Fey mood → logged, workshop availability checked
- Dwarf enters Possessed mood → same handling
- Moody dwarf completes artifact → artifact logged, added to tracking set, displayed on pedestal
- Moody dwarf goes insane → failure logged, removed from moody map
- Forbidden bones present during mood → unforbidden automatically
- 50+ citizens reached → museum zone created
- Multiple artifacts exist → each assigned to separate display furniture
- No free display furniture → additional pedestal production triggered
