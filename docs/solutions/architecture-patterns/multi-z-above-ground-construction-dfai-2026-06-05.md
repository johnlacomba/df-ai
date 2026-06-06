---
title: Multi-Z Above-Ground Construction System for df-ai
date: 2026-06-05
category: architecture-patterns
module: df-ai above-ground construction
problem_type: architecture_pattern
component: tooling
severity: medium
applies_when:
  - Building above-ground multi-story structures in DFHack df-ai
  - Room templates require outdoor or open-air tiles across multiple Z-levels
  - Temporary scaffolding must be placed and reliably removed after construction completes
  - Construction ordering across Z-levels must be enforced to prevent floating structures
tags:
  - dfhack
  - df-ai
  - above-ground
  - multi-z
  - construction
  - scaffolding
  - room-templates
  - fixup-open
---

# Multi-Z Above-Ground Construction System for df-ai

## Context

The df-ai plugin had no capability to build above-ground multi-story structures. Underground rooms are dug out of rock, but above-ground construction requires building walls, floors, and fortifications tile-by-tile in open air, with strict bottom-up ordering (dwarves need something to stand on) and temporary access (scaffolding).

The guard tower feature required a 3-story above-ground structure with constructed walls, fortifications, a central staircase, and barracks integration — entirely new territory for the plugin.

## Guidance

### The `fixup_open()` mechanism drives above-ground construction

The existing `fixup_open()` function in `plan.cpp` already handles above-ground tile construction. When a room template declares tiles with `dig: "No"` (should be a wall) or furniture entries with explicit `construction` types, `fixup_open()` detects that outdoor tiles are Open when they should be Wall/Floor and creates `furnish` tasks to construct them. The pitting tower template (`rooms/templates/generic01_pitting_tower/generic.json`) proves this works across 10+ Z-levels.

**Key template flags for outdoor multi-Z rooms:**
- `outdoor: true` — tells `can_add_room()` to validate surface placement
- `require_floor: false` — allows tiles above ground level (open air) to pass validation. Without this, every tile must be at the ground surface Z-level

### Room template structure

Define the tower as a JSON template with `"f"` (furniture/construction entries) and `"r"` (room definitions):

```json
{
  "f": [
    {"construction": "Wall", "dig": "No", "x": 0, "y": 0, "z": 0},
    {"construction": "Fortification", "dig": "No", "x": 0, "y": 0, "z": 1},
    {"construction": "Floor", "x": 1, "y": 1, "z": 1},
    {"construction": "UpDownStair", "x": 4, "y": 4, "z": 0},
    {"construction": "UpDownStair", "x": 4, "y": 4, "z": 1, "scaffolding": true}
  ],
  "r": [{
    "type": "barracks",
    "min": [0, 0, 0], "max": [8, 8, 2],
    "outdoor": true,
    "require_floor": false,
    "layout": [0, 1, 2, 3, 4]
  }]
}
```

### Multi-Z construction ordering

`fixup_open()` processes all tiles in a room. For multi-Z outdoor rooms, it would create construction tasks for Z+2 tiles before Z+0 walls are built — but dwarves can't reach Z+2 without Z+0 walls in place. The fix: scan each Z-level bottom-up and only allow construction at Z+N if all declared constructions at Z+(N-1) are complete:

```cpp
int16_t max_buildable_z = r->max.z;
if (r->outdoor && r->min.z != r->max.z)
{
    for (int16_t z = r->min.z; z < r->max.z; z++)
    {
        // check if this Z-level has any Open tiles that should be constructed
        // if so, cap max_buildable_z here
    }
}
```

### Scaffolding lifecycle

Scaffolding stairs (temporary wooden UpDownStairs) provide construction access to upper floors. The critical lifecycle issue: **construction-only furniture (type=none) bypasses `check_furnish`**. When `try_furnish_construction` succeeds for a furniture entry with `type == layout_type::none`, the furnish task returns true immediately — no `check_furnish` task is created, so `try_endfurnish` is never called.

**The fix:** When scaffolding construction completes in `try_furnish`, explicitly create a `check_furnish` task and set `bld_id` to a non-negative value. Then in `try_endfurnish`, check scaffolding BEFORE the building lookup (scaffolding constructions aren't buildings). The scaffolding handler polls `constructions_done()` and calls `AI::dig_tile()` to remove the stair when all real constructions are complete.

Also critical: `constructions_done()` must skip scaffolding entries BEFORE calling `Maps::getTileType()` — if the scaffolding tile is at a map edge or unloaded, it would return false and permanently block room completion.

### Serialization

The `scaffolding` flag must be saved/loaded in `plan_persist.cpp`. Without this, a save/load cycle resets all scaffolding flags to false, causing `constructions_done()` to check scaffolding tiles as regular constructions.

## Why This Matters

Without these patterns, above-ground multi-story buildings either can't be built (no construction ordering), get stuck permanently (scaffolding never removed or blocks completion checks), or corrupt after save/load (flag not persisted). Each of these failure modes was discovered during implementation or code review.

## When to Apply

- Building any above-ground structure taller than 1 Z-level
- Adding new furniture flags that affect construction lifecycle
- Creating room templates with `outdoor: true` and multi-Z spans
- Any feature that uses temporary constructions that must be removed

## Examples

The guard tower template (`rooms/templates/generic01_guardtower/generic.json`) demonstrates all patterns:
- 9x9 footprint, 3 Z-levels (barracks Z+0, fortification corridor Z+1, fortification roof Z+2)
- 214 furniture entries (walls, floors, fortifications, stairs, barracks furniture)
- 4 scaffolding entries at Z+1 and Z+2 corners with `"scaffolding": true`
- Central permanent staircase at position (4,4) spanning all 3 levels

The pitting tower template (`rooms/templates/generic01_pitting_tower/generic.json`) is the simpler precedent — 10 Z-levels but only stairs and floors, no walls or fortifications.

## Related

- [Guard tower plan](docs/plans/2026-06-05-001-feat-guard-tower-plan.md) — originating plan document
- `plan.cpp` — `fixup_open()` and `fixup_open_tile()` 
- `plan_construct.cpp` — `try_furnish`, `try_endfurnish`, `furnish_room`
- `room.cpp` — `constructions_done()`
- `plan_persist.cpp` — furniture flag serialization
