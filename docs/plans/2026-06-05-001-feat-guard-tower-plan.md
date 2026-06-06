---
title: "feat: Build above-ground guard tower with barracks and ranged fortifications"
type: feat
status: active
date: 2026-06-05
---

# feat: Build above-ground guard tower with barracks and ranged fortifications

## Overview

Add a 3-story above-ground guard tower near the fortress entrance. First floor is a barracks for squad training, second floor has fortification walls with a forced inner corridor for ranged combat, third floor is a fortification roof. Includes scaffolding logic for multi-story construction and integration with the military system.

---

## Problem Frame

The AI builds no defensive above-ground structures. When invaders arrive, there is no elevated position for ranged dwarves to fire from, and no hardened structure near the entrance. The guard tower provides both a training barracks and a tactical ranged platform.

---

## Requirements Trace

- R1. Three-story tower: ground floor barracks, second floor ranged platform, third floor roof
- R2. First floor large enough for a 10-dwarf squad (minimum 49 interior tiles)
- R3. Second floor: outer fortification walls, 1-tile inner corridor forcing dwarves against fortifications, inner walls, central staircase
- R4. Third floor: fortifications across the top acting as ceiling for second floor
- R5. Central staircase connecting all floors (not at edges — invaders can't instantly access upper floors)
- R6. Scaffolding system using wooden up/down stairs, torn down top-to-bottom after completion
- R7. Prefer stone (blocks/boulders) for construction, allow wood as fallback
- R8. Place near fortress entrance, accounting for trap placement
- R9. Barracks zone on first floor with squad assignment for training
- R10. Bottom-up build order: ground floor walls → floor above → second floor → roof

---

## Scope Boundaries

- The tower is a static defensive structure, not a modular/configurable design
- No murder holes, drawbridges, or moat integration in v1
- No automatic squad stationing orders — just the barracks zone and assignment
- No siege engine (ballista/catapult) placement on the roof
- Roof fortifications may not fully block rain (DF fortification mechanics) — this is acceptable

### Deferred to Follow-Up Work

- Intelligent squad stationing during sieges (separate military AI feature)
- Archer skill-based assignment to upper floors
- Multiple tower support or tower upgrades
- Integration with civilian alert/burrow system

---

## Context & Research

### Relevant Code and Patterns

- `rooms/templates/generic01_pitting_tower/generic.json` — the only existing multi-Z outdoor structure (spans 11 Z-levels). Uses `outdoor: true`, `require_floor: false`, constructed stairs, and `fixup_open()` auto-repair for constructions
- `rooms/templates/generic01_depot/open.json` — simple outdoor template with `outdoor: true`, `in_corridor: true`
- `rooms/templates/generic01_barracks/north.json` — barracks furniture layout (weapon_rack with `makeroom: true`, armor_stands, beds, etc.)
- `plan_construct.cpp:663-896` — `try_furnish_construction()` builds walls, floors, fortifications, stairs one tile at a time using blocks (preferred) or boulders (fallback)
- `plan_construct.cpp:1581-1756` — `ConstructActivityZone` creates civzones
- `plan_assign.cpp:229-328` — `getsoldierbarrack()` and `assign_barrack_squad()` for military integration
- `plan.cpp` — `fixup_open()` / `fixup_open_tile()` auto-creates construction tasks when outdoor room tiles should be walls but are open air. This is the key mechanism: the template declares what each tile should be via `dig: "No"` (wall) or `dig: "Default"` (floor), and `fixup_open()` handles generating the actual construction jobs
- `plan_setup_blueprint.cpp` — outdoor room placement via `surface_tile_at()`, `can_add_room()` validation

### Key Mechanism: `fixup_open()` drives above-ground construction

The existing system already handles multi-Z above-ground building. The template JSON declares furniture entries with `construction` types (Wall, Floor, Fortification, UpDownStair), and the room's tiles are marked with `dig: "No"` for walls. When `fixup_open()` runs on the room, it detects that outdoor tiles are Open when they should be Wall/Floor, and creates `furnish` tasks to construct them. The pitting tower uses exactly this mechanism for its 10-Z-level stair shaft.

This means the guard tower template doesn't need new construction logic — it just needs the right furniture/construction entries and `dig` designations. The existing task pipeline handles everything.

### External References

- [DF Wiki: Tower project](https://dwarffortresswiki.org/index.php/Tower_(project)) — standard tower construction patterns
- [DF Wiki: Fortification](https://dwarffortresswiki.org/index.php/Fortification) — fortifications allow projectiles through, block creature movement
- [DF Wiki: Barracks](https://dwarffortresswiki.org/index.php/Barracks) — barracks zone mechanics in Steam DF

---

## Key Technical Decisions

- **9x9 exterior footprint**: Provides 7x7 interior on first floor (49 tiles, fits 10 dwarves). Second floor: 9x9 perimeter fortifications, 1-tile corridor, 5x5 inner walls, 3x3 inner room with staircase. This is the minimum size for the corridor+inner-wall+staircase layout
- **Template-based, not code-based**: Define the tower as a room template JSON file (like the pitting tower), not custom C++ construction code. Leverages the existing `fixup_open()` mechanism
- **Minimal scaffolding leveraging wall tops**: Wall tops are implicitly walkable in DF, so dwarves can stand on Z+0 walls to build Z+1 constructions, and on Z+1 walls to build Z+2. The permanent central staircase provides interior access. Scaffolding stairs (wooden, temporary) are only needed at Z+2 to let dwarves reach the outermost roof fortification tiles that aren't adjacent to the staircase or inner walls. One or two temporary wooden UpDownStairs at Z+2 corners suffice
- **Permanent central staircase**: Stone up/down stairs at position (4,4) relative to room min, spanning Z+0 to Z+2. This is the permanent access. Invaders must enter the door, cross the barracks, and climb stairs — no direct exterior access to upper floors
- **Blocks preferred**: The construction system already prefers blocks over boulders. A 9x9 3-story tower needs ~120+ construction tiles. The AI should ensure the mason's workshop produces enough blocks
- **Placement via outdoor phase**: Added to the blueprint system's outdoor room placement, using the same `surface_tile_at()` mechanism as the trade depot and pitting tower

---

## Open Questions

### Resolved During Planning

- **Do fortifications provide a ceiling?** No — constructed fortifications are open on top. The third floor is all-fortification intentionally so ranged dwarves stationed there can fire over them. No additional floor or ceiling above — the open top is the desired behavior
- **Can dwarves build on wall tops?** Yes — wall tops are implicitly walkable in DF. Dwarves can stand on a wall to build the floor/wall above it. This eliminates most scaffolding needs for exterior walls
- **How does `fixup_open()` handle multi-Z?** It processes all tiles in the room and checks each against the declared `dig` designation. For Z+1 and Z+2 tiles that are open air but should be walls/floors, it creates construction tasks. The pitting tower proves this works across 10+ Z-levels

### Deferred to Implementation

- Exact scaffolding corner positions at Z+2 — two opposite corners provide maximum coverage but exact coordinates depend on the inner wall layout
- Whether `build_when_accessible` flag is needed on upper-floor rooms to prevent premature construction attempts
- Whether a separate corridor room at Z+0 is needed for the surrounding clear area (like the depot template) or if the tower can be standalone

---

## High-Level Technical Design

> *This illustrates the intended approach and is directional guidance for review, not implementation specification.*

### Tower Layout (all coordinates relative to room min)

**Z+0 — Ground Floor (Barracks):**
```
WWWWWWWWW    W = Constructed Wall (dig: "No", construction: "Wall")
W.......W    . = Natural/Constructed Floor (dig: "Default")
W.......W    D = Door
W.......W    S = Constructed UpDownStair (construction: "UpDownStair")
W...S...W    
W.......W    Interior: 7x7 = 49 tiles (barracks zone)
W.......W
W.......W
WWWWDWWWW
```

**Z+1 — Second Floor (Ranged Platform):**
```
FFFFFFFFF    F = Constructed Floor (construction: "Floor")
XXXXXXXXX    X = Constructed Fortification (construction: "Fortification")
X_______X    _ = Constructed Floor (construction: "Floor") — corridor
X_OOOOO_X    O = Constructed Wall (construction: "Wall") — inner walls
X_O...O_X    . = Constructed Floor (construction: "Floor") — inner room
X_O.S.O_X    S = Constructed UpDownStair
X_OOOOO_X
X_______X
XXXXXXXXX
```
Note: Floor tiles (F) at the very top of Z+1 serve as the ceiling of Z+0. All tiles at this level need constructed floors first, then walls/fortifications on top.

Wait — in DF, floors and walls occupy the same Z-level. A wall at Z+1 provides a walkable surface at Z+1. A floor at Z+1 provides a walkable surface at Z+1. The "ceiling" of Z+0 is the floor/wall at Z+1.

Revised Z+1:
```
XXXXXXXXX    X = Constructed Fortification
X_______X    _ = Constructed Floor (corridor)
X_OOOOO_X    O = Constructed Wall (inner)
X_O...O_X    . = Constructed Floor (inner room)
X_O.S.O_X    S = Constructed UpDownStair
X_OOOOO_X
X_______X
XXXXXXXXX
```

**Z+2 — Roof (Fortification Ceiling):**
```
XXXXXXXXX    X = Constructed Fortification
X_______X    _ = Constructed Floor
X_FFFFF_X    F = Constructed Floor (covers inner room)
X_F...F_X    
X_F.s.F_X    s = Constructed DownStair (top of staircase)
X_FFFFF_X
X_______X
XXXXXXXXX
```

Actually, re-reading the user's request: "fortifications on the third floor acting as a roof and ceiling for the second floor." The entire Z+2 should be fortifications. Let me simplify:

```
XXXXXXXXX    X = Constructed Fortification (entire floor)
XXXXXXXXX    Acts as ceiling for Z+1 and
XXXXXXXXX    provides additional ranged positions
XXXXXXXXX
XXXX↓XXXX    ↓ = DownStair (top of staircase)
XXXXXXXXX
XXXXXXXXX
XXXXXXXXX
XXXXXXXXX
```

### Construction Sequence

```
Phase 1: Ground Floor (Z+0)
├── Construct walls around 9x9 perimeter
├── Construct door at entrance
├── Construct UpDownStair at center (4,4)
└── Create barracks zone over interior

Phase 2: Second Floor (Z+1)
├── Construct floors over Z+0 walls (dwarves walk on wall tops)
├── Construct fortifications around 9x9 perimeter
├── Construct floors for 1-tile corridor
├── Construct inner walls (5x5 perimeter)
├── Construct floors for inner room
└── Construct UpDownStair at center (4,4)

Phase 3: Roof (Z+2)
├── Build 2 scaffolding stairs (wooden UpDownStairs at opposite corners)
├── Construct fortifications across entire 9x9 (dwarves build from wall tops + scaffolding)
├── Construct DownStair at center (4,4)
└── Remove 2 scaffolding stairs

Phase 4: Military Integration
├── Assign barracks zone to a squad
└── Place weapon racks, armor stands for training
```

---

## Implementation Units

- U1. **Guard tower room template**

**Goal:** Create the JSON template defining the 3-story guard tower with all construction entries, furniture, and room definitions.

**Requirements:** R1, R2, R3, R4, R5, R7

**Dependencies:** None

**Files:**
- Create: `rooms/templates/generic01_guardtower/generic.json`

**Approach:**
- Follow the pitting tower template pattern: `"f"` array for furniture/constructions, `"r"` array for rooms
- Define three room entries spanning three Z-levels:
  - Room 0: Barracks room at Z+0, 9x9, `outdoor: true`, with door, weapon_racks, armor_stands
  - Room 1: Corridor at Z+1, 9x9, `outdoor: true`, `require_floor: false` — second floor
  - Room 2: Corridor at Z+2, 9x9, `outdoor: true`, `require_floor: false` — roof
- Construction furniture entries (~120 entries): walls at Z+0 perimeter, fortifications at Z+1 perimeter, inner walls at Z+1, floors throughout, fortifications at Z+2, stairs at center column
- Door furniture entry at Z+0 entrance
- Barracks furniture: weapon_rack (makeroom), armor_stands, beds for the squad
- Use `dig: "No"` for wall/fortification positions to trigger `fixup_open()` construction
- Scaffolding stairs: 4 temporary wooden UpDownStair entries at the corners of Z+1 and Z+2, marked for later removal

**Patterns to follow:**
- `rooms/templates/generic01_pitting_tower/generic.json` — multi-Z outdoor template with constructed stairs and `require_floor: false`
- `rooms/templates/generic01_barracks/north.json` — barracks furniture layout
- `rooms/templates/generic01_depot/open.json` — outdoor room with `in_corridor: true`

**Test scenarios:**
- Happy path: Template loads without JSON schema errors; all furniture indices are valid; room min/max spans cover the full 9x9x3 area
- Edge case: Furniture positions don't overlap (no two constructions at the same tile)
- Edge case: Staircase entries form a continuous shaft from Z+0 to Z+2

**Verification:**
- Template loads successfully when the blueprint system initializes
- All construction types (Wall, Floor, Fortification, UpDownStair, DownStair) are represented
- Barracks furniture (weapon_rack with makeroom, armor_stands) is present

---

- U2. **Guard tower placement and blueprint integration**

**Goal:** Add the guard tower to the blueprint system's outdoor placement phase, positioned near the fortress entrance and clear of trap corridors.

**Requirements:** R8

**Dependencies:** U1

**Files:**
- Modify: `plans/generic01.json` — add `generic01_guardtower` to tags, limits, and priorities
- Modify: `rooms/instances/` — create instance file if needed (may not be needed if template is standalone like pitting tower)

**Approach:**
- Add `generic01_guardtower` to the `generic01_wide` tag list (same as depot, pitting tower) so it's placed during the outdoor phase
- Set limits to `[1, 1]` — exactly one guard tower per fortress
- Add a priority entry with `dig_immediate` action near the top of the priorities list (after the starting workshops but before deep-dig items) to ensure the tower gets built early
- Placement: the blueprint system's outdoor placement randomly positions outdoor rooms. The guard tower should ideally be near the outpost entrance, but the random placement with `surface_tile_at()` validation should work — the key constraint is `outdoor: true` and sufficient flat surface area

**Patterns to follow:**
- `plans/generic01.json` entries for `generic01_pitting_tower` and `generic01_depot`

**Test scenarios:**
- Happy path: Guard tower appears in the blueprint and gets placed on the surface
- Edge case: Tower placement doesn't overlap with trade depot or trap corridor
- Error path: If no valid 9x9 surface area exists, tower placement fails gracefully (skipped, not crash)

**Verification:**
- Guard tower room appears in the plan's room list after blueprint setup
- Tower is placed at a valid outdoor surface location

---

- U3. **Multi-Z construction ordering**

**Goal:** Ensure the tower is constructed bottom-up: Z+0 walls and floor first, then Z+1, then Z+2. The existing `fixup_open()` mechanism handles individual tile construction, but multi-Z ordering needs to ensure dwarves can reach upper tiles.

**Requirements:** R10

**Dependencies:** U1, U2

**Files:**
- Modify: `plan.cpp` — adjust `fixup_open()` or `fixup_open_tile()` to prioritize lower-Z construction tasks
- Possibly modify: `plan_construct.cpp` — add Z-level ordering to furnish task scheduling

**Approach:**
- The existing `fixup_open()` processes all tiles in a room. For a multi-Z room, it may create construction tasks for Z+2 tiles before Z+0 walls are built. Dwarves can't reach Z+2 tiles without the Z+0 walls and Z+1 floors being in place first
- Add a check in `fixup_open_tile()`: for outdoor rooms with multi-Z spans, only create construction tasks for Z+N tiles if all tiles at Z+(N-1) in the same room are already constructed (or natural ground)
- Alternatively, use `build_when_accessible` on the upper-floor rooms — the existing system checks this flag before starting construction

**Patterns to follow:**
- `plan_construct.cpp` — `constructions_done()` check that gates workshop construction behind construction completion

**Test scenarios:**
- Happy path: Z+0 walls are constructed before Z+1 floors are attempted
- Edge case: If some Z+0 walls are natural terrain (hills), Z+1 construction starts above them while waiting for remaining Z+0 walls
- Error path: Construction stalls if materials run out mid-floor — resumes when materials arrive

**Verification:**
- No construction tasks are created for Z+1 or Z+2 while Z+0 walls are still being built
- Dwarves can pathfind to all construction sites

---

- U4. **Scaffolding system**

**Goal:** Build temporary wooden up/down stairs for construction access to upper floors, then remove them top-to-bottom after the tower is complete.

**Requirements:** R6

**Dependencies:** U1, U3

**Files:**
- Modify: `room.h` — add `scaffolding` flag to furniture struct (or reuse `temporary` if it exists)
- Modify: `plan_construct.cpp` — add scaffolding teardown logic in construction completion handler
- Modify: `plan_task.cpp` — add `task_type::remove_scaffolding` or handle in existing task flow

**Approach:**
- Wall tops are walkable in DF, so Z+0 and Z+1 construction needs no scaffolding — dwarves stand on wall tops and the permanent central staircase
- Only Z+2 (roof) needs scaffolding: 2 temporary wooden UpDownStairs at opposite corners of Z+2, giving dwarves access to the outermost roof tiles
- In the template, mark these 2 stair entries with a `scaffolding: true` flag
- Scaffolding stairs use wood (`items_other_id::WOOD`) instead of stone/blocks
- `try_furnish_construction()` already handles stairs — just need to prefer wood items for scaffolding-flagged entries
- When all non-scaffolding constructions in the room are complete, trigger scaffolding removal:
  1. Use `AI::dig_tile(t)` to designate constructed stair removal at Z+2 (this queues a remove-construction job)
  2. Only 2 stairs to remove, both on the same Z-level — no multi-Z teardown sequencing needed
- The permanent central staircase (stone) is NOT marked as scaffolding — it stays

**Patterns to follow:**
- `plan_assign.cpp:368` — existing `Buildings::deconstruct()` usage for furniture removal
- `plan_priorities.cpp:876` — existing construction removal patterns

**Test scenarios:**
- Happy path: Scaffolding stairs are built before upper-floor construction begins; removed after tower is complete
- Edge case: If a dwarf is standing on a scaffolding stair during removal, the removal job waits
- Error path: If scaffolding removal fails (item stuck), the stairs remain — non-critical since they're just extra stairs

**Verification:**
- Scaffolding stairs exist during construction phase
- After tower completion, scaffolding stairs are removed and only the permanent central staircase remains

---

- U5. **Barracks zone and military integration**

**Goal:** Create a barracks civzone on the first floor and assign the first available squad to train there.

**Requirements:** R9

**Dependencies:** U1, U3

**Files:**
- Modify: `plan_construct.cpp` — add barracks zone creation for guard tower rooms (extend `ConstructActivityZone` or add to `try_endfurnish`)
- Modify: `plan_assign.cpp` — ensure `getsoldierbarrack()` can find the guard tower barracks

**Approach:**
- When the guard tower's first floor room (type: barracks) finishes construction, create a `building_civzonest` with `civzone_type::Barracks` (or use the existing weapon_rack `makeroom` pattern)
- The existing `assign_barrack_squad()` function handles both the squad-side (`squad->rooms`) and zone-side (`zone->squad_room_info`) linkage
- The guard tower barracks should be preferred over underground barracks for the first squad — add logic to `getsoldierbarrack()` to prefer above-ground barracks
- Barracks furniture from the template: weapon_rack (makeroom), armor_stands, optional beds/cabinets for individual soldier assignment

**Patterns to follow:**
- `plan_construct.cpp:2120-2133` — existing barracks `try_endfurnish()` handler
- `plan_assign.cpp:229-328` — `getsoldierbarrack()` and `assign_barrack_squad()`
- `rooms/templates/generic01_barracks/north.json` — barracks furniture layout

**Test scenarios:**
- Happy path: Barracks zone is created after first floor construction completes; squad is assigned
- Edge case: If no squad exists yet, barracks zone is created but unassigned; squad assigned when military code creates one
- Integration: Squad members path to the guard tower barracks for training

**Verification:**
- A barracks civzone exists covering the guard tower first floor interior
- A squad is assigned to train at the barracks
- Soldiers can path to the barracks and begin training

---

- U6. **Priority and early construction**

**Goal:** Ensure the guard tower is built early enough to be useful before the first siege, and that sufficient building materials (blocks) are available.

**Requirements:** R7, R8

**Dependencies:** U2

**Files:**
- Modify: `plans/generic01.json` — add priority entry for guard tower
- Possibly modify: `stocks.cpp` — increase block production when guard tower is planned

**Approach:**
- Add a priority entry for the guard tower with `action: "dig_immediate"` — even though it's not digging, this triggers immediate room processing
- The priority should fire after basic workshops are built (need mason's workshop for blocks) but before deep fortress expansion
- The tower needs ~120+ blocks. Current block production may be insufficient. Consider adding a manager order for blocks when the guard tower is queued — the stocks system's `MakeBlocks` order with a higher quantity
- The priority check: `count` of `guardtower` rooms with `status_not: plan` is 0

**Patterns to follow:**
- `plans/generic01.json` priority entries for depot, pitting tower

**Test scenarios:**
- Happy path: Guard tower construction begins within the first few months
- Edge case: If block production is slow, tower construction pauses but doesn't block other fortress operations

**Verification:**
- Guard tower appears in the priority list and starts processing early
- Sufficient blocks are produced to complete construction

---

## System-Wide Impact

- **Construction pipeline**: The guard tower uses the existing `fixup_open()` and `try_furnish_construction()` pipeline. Multi-Z ordering (U3) is the main new behavior — care needed to not break existing multi-Z rooms (pitting tower, staircase)
- **Military system**: The guard tower barracks integrates via existing `assign_barrack_squad()`. No changes to squad creation, equipment, or scheduling
- **Material demand**: ~120+ blocks is a significant material draw early in the game. The mason's workshop and stone supply must keep up. This could delay other construction (furniture, walls) if blocks are diverted
- **Blueprint placement**: Adding a new 9x9 outdoor room increases surface area demand. On small or hilly embarks, placement may fail — this should degrade gracefully (skip tower, not crash)
- **Unchanged invariants**: Underground barracks, military drafting, squad equipment, trade depot, and all other existing systems are unaffected

---

## Risks & Dependencies

| Risk | Mitigation |
|------|------------|
| Dwarves can't pathfind to upper-floor construction sites | Scaffolding stairs (U4) provide temporary access; permanent central staircase also helps |
| Block shortage delays tower completion significantly | Increase block production order quantity; tower construction is non-blocking for other systems |
| Tower placement fails on hilly/forested terrain | `can_add_room()` validation rejects bad placements; tower is skipped, not crashed |
| `fixup_open()` creates all Z-level tasks simultaneously | U3 adds Z-level ordering to prevent premature upper-floor construction |
| Scaffolding removal traps a dwarf on roof | Only 2 stairs at Z+2 to remove; permanent staircase verified complete first; dwarves can still exit via central stair |
| Fortification roof doesn't block weather | Acceptable — DF fortifications are inherently open. Dwarves don't care about rain |

---

## Sources & References

- Codebase: `rooms/templates/generic01_pitting_tower/generic.json` — multi-Z outdoor precedent
- Codebase: `plan_construct.cpp:663-896` — construction tile building
- Codebase: `plan_assign.cpp:229-328` — barracks squad assignment
- Codebase: `plan.cpp` — `fixup_open()` mechanism
- [DF Wiki: Tower project](https://dwarffortresswiki.org/index.php/Tower_(project))
- [DF Wiki: Fortification](https://dwarffortresswiki.org/index.php/Fortification)
