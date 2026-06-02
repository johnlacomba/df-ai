---
title: Furniture production regression from ManagerOrderExclusive removal and missing assignments_by_type cache update
date: "2026-06-01"
category: logic-errors
module: df-ai-stocks
problem_type: logic_error
component: tooling
severity: high
symptoms:
  - "Work orders showed as validated/active but nothing was dispatched to workshop queues"
  - "\"A manager is required to add work orders to the shop\" message appeared despite manager being appointed"
  - "Chair bootstrap via Items::createItem produced unplaceable items with invalid material parameters"
  - "Noble assignment appeared correct in UI but manager was missing from DF internal lookup table"
root_cause: logic_error
resolution_type: code_fix
related_components:
  - service_object
tags:
  - dfhack
  - df-ai
  - manager-orders
  - noble-assignment
  - regression
  - bisection
  - furniture-production
  - dwarf-fortress
---

# Furniture production regression from ManagerOrderExclusive removal and missing assignments_by_type cache update

## Problem

A DFHack AI plugin for Dwarf Fortress Steam edition suffered a production regression where dwarves stopped building furniture (chairs, beds, doors) in workshops, despite buildings being constructed and work orders appearing in the manager queue. Two independent root causes compounded to make diagnosis extremely difficult.

## Symptoms

- Work orders showed as validated/active in the manager UI but nothing was dispatched to workshop queues
- "A manager is required to add work orders to the shop" message appeared even though a manager was appointed
- Stone armor stands were being spammed in workshops but items never materialized in stock counts
- Thrones were "created" by `Items::createItem` but couldn't be placed and didn't appear in furniture stockpiles
- Manager was appointed but lacked an office, creating a chicken-and-egg problem (no office without furniture, no furniture without manager)

## What Didn't Work

1. **Setting `status.bits.validated = true` and `status.bits.active = true` on manager orders** -- Helped orders appear active in the UI but wasn't the core issue; orders still weren't dispatched to workshops.

2. **Creating raw `df::job` objects directly at mason workshops** -- Jobs ran their animations but didn't produce actual items because they lacked proper `job_items` material requirements. The stock counts never increased.

3. **Using `Items::createItem(created, creator, item_type::CHAIR, -1, 0, 0)` to spawn chairs** -- `mat_type=0, mat_index=0` creates an item from the first inorganic material in the world data, which is not necessarily a valid stone. The resulting items couldn't be placed, hauled, or stored in furniture stockpiles.

4. **Adding `Job::checkBuildingsNow()` after order creation** -- Useful auxiliary step for prompting DF to re-evaluate workshop assignments, but not the fix.

5. **Removing `ManagerOrderExclusive` class and inlining order creation into `add_manager_order`** -- Broke deduplication across the entire stocks system (`stocks_equipment.cpp`, `stocks_forge.cpp`, `stocks_queue.cpp`), causing order spam and resource exhaustion.

## Solution

The fix was a methodical bisection approach:

1. **Reverted all affected source files** to the last known working commit (`125e9c97`).
2. **Re-applied changes incrementally**, grouped by risk level, with user testing after each group:
   - Group 1: Null-check bug fixes (passed)
   - Group 2: Pause fix (passed)
   - Group 3: Camera system (passed)
   - Group 4: Noble assignment refactor (FAILED -- then fixed by restoring `assignments_by_type`)
   - Group 5: Trading system (passed)
   - Group 6: Manager/chair bootstrap changes (left out -- `ManagerOrderExclusive` kept intact)
3. **Kept the `ManagerOrderExclusive` event-queue system intact** rather than inlining order creation.
4. **Restored the `assignments_by_type` cache update** when re-applying the noble refactor.

### Key fix -- the critical line that was removed in the noble refactor:

```cpp
// In population_nobles.cpp, after appointing a noble:
entity->assignments_by_type[responsibility].push_back(asn);
```

This line updates DF's internal lookup cache so it can find the manager through its normal code path. Without it, the manager appears assigned in the UI but DF's `assignments_by_type` map doesn't know about it, so work orders are never dispatched to workshops.

## Why This Works

### Root Cause 1: ManagerOrderExclusive provided essential deduplication

The `ManagerOrderExclusive` class was an event-queue-based system for creating manager orders. Multiple systems used `events.each_exclusive<ManagerOrderExclusive>()` to check for pending-but-not-yet-created orders before queueing new ones. This prevented duplicate orders for weapons, armor, clothes, forging, smelting, and gem cutting. Removing the class and all its `events.each_exclusive` calls eliminated this deduplication, causing the AI to spam orders and exhaust resources.

Files that depend on `ManagerOrderExclusive` deduplication:
- `stocks_manager.cpp` -- `count_manager_orders()` and `count_manager_orders_matcat()`
- `stocks_equipment.cpp` -- weapon, armor, and clothes ordering
- `stocks_forge.cpp` -- metal forging, smelting, and coke processing
- `stocks_queue.cpp` -- gem cutting

### Root Cause 2: assignments_by_type is DF's internal noble lookup cache

DF stores noble position assignments in `entity->positions.assignments`, but uses `entity->assignments_by_type[responsibility]` as a fast lookup cache indexed by responsibility type. The noble assignment refactor added histfig link management and history event generation but removed the cache update. The UI reads from `positions.assignments` (so the noble appears assigned), but the game logic that dispatches work orders reads from `assignments_by_type` (so no manager is found).

## Prevention

1. **Always update `assignments_by_type` when assigning nobles.** DF's UI and game logic use different data paths -- the UI shows `positions.assignments` but work order dispatch uses `assignments_by_type`. Removing the cache update creates a silent failure where everything looks correct in the UI but nothing works.

2. **Never remove event-queue deduplication systems without an equivalent replacement.** The `ManagerOrderExclusive` class and its `events.each_exclusive` calls are load-bearing across 5+ files. If the system needs refactoring, the replacement must provide the same deduplication guarantees.

3. **Use bisection for complex regressions in C++ plugin code.** Revert to a known-good commit and re-apply changes incrementally, testing after each group. This is more reliable than trying to reason about which change broke things when multiple changes interact.

4. **`Items::createItem` material parameters must be validated.** `mat_type=0, mat_index=0` does not create a usable stone item -- index 0 is just the first entry in `world->raws.inorganics`, which could be anything. Material indices must be validated against actual inorganic materials.

5. **Direct `df::job` creation at workshops requires proper `job_items`.** Without material requirement entries, the job animation runs but DF doesn't produce actual items. The stock system never sees the output.

## Related Issues

- `docs/incremental-changes-since-125e9c97.md` -- Bisection scratchpad created during the debugging process, documenting all commits between the working build and the regression
- `docs/plans/2026-05-27-001-feat-port-df-ai-to-steam-dfhack-plan.md` -- Parent plan (U8: Stocks system, U10: Population/nobles) that specified the refactoring strategy where the regression was introduced
