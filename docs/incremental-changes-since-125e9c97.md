# Changes since 125e9c97 (last known working build)

Baseline commit: `125e9c97007db033985eccbe5166b407db6738d1`
Branch: `feat/port-to-steam-dfhack-53x`

Re-apply one group at a time. Test after each group that furniture is still being built.

---

## Group 1: Camera system (low risk — no stocks/plan logic)

### 445d661 — camera plan doc + military cleanup
- Files: `docs/plans/2026-05-30-003-feat-event-driven-camera-plan.md`, `population_military.cpp`
- Adds camera plan doc, removes old military code

### 0031ef3 — camera event queuing
- Files: `camera.cpp`, `camera.h`, `military.cpp`, `plan_construct.cpp`, `plan_task.cpp`, `population.cpp`, `population_moods.cpp`
- Adds event queue to camera, adds `queue_camera_event` calls in plan_construct, plan_task, population, population_moods

### 635ba31 — ghost/lost citizen camera events
- Files: `camera.cpp`, `population.cpp`, `population.h`, `population_death.cpp`
- Tracks ghosts and lost citizens with camera events

### 45dca01 — enemy tagging camera events
- Files: `military.cpp`
- Enhanced enemy tagging with threat reasons and event logging

### 0c9cb4c — camera dwell management refactor
- Files: `camera.cpp`, `camera.h`
- Refactors dwell management and event handling

### d6a8533 — remove camera event queueing
- Files: `plan_construct.cpp`, `plan_task.cpp`, `population.cpp`, `population_death.cpp`, `population_moods.cpp`, `trade_manager.cpp`
- Removes the `queue_camera_event` calls added in 0031ef3 (reverts that part)

**Note:** 0031ef3 adds camera calls to plan_construct/plan_task/population, then d6a8533 removes them. These two should be applied together.

---

## Group 2: Null-check bug fixes (low risk — safety only)

### 93851eb — multiple null-check fixes
- Files: `camera.cpp`, `pause.cpp`, `plan_assign.cpp`, `plan_cistern.cpp`, `population.cpp`, `population_nobles.cpp`, `stocks_find.cpp`, `stocks_manager.cpp`, `stocks_update.cpp`
- camera.cpp: null check for unit tracking
- pause.cpp: close multiple interface alerts on unpause
- plan_assign.cpp: safety checks for pet grazing calculations
- plan_cistern.cpp: check item existence before processing
- population.cpp: null unit reference safeguards
- population_nobles.cpp: early return if unit has no current soul
- stocks_find.cpp: null vehicle reference check
- stocks_manager.cpp: `delete` old order after erasing (memory leak fix)
- stocks_update.cpp: null checks in slab description logging

---

## Group 3: Pause fix

### 2122710 — enhanced unpause logic
- Files: `pause.cpp`
- Close multiple open interfaces when unpausing

---

## Group 4: Noble assignment refactor (medium risk — touches noble/manager assignment)

### e215387 — refactor noble assignment
- Files: `population_nobles.cpp`
- Major rewrite: 128 lines removed, 15 added. Uses new position assignment structure.

### 25c1347 — add assignment handling
- Files: `population_nobles.cpp`
- 2 lines added for noble responsibility handling

### e4e7c51 — historical figure link removal
- Files: `population_nobles.cpp`
- Handles historical figure link removal during noble reassignment

### 8fe4319 — historical figure link management
- Files: `population_nobles.cpp`
- Full link management and event generation for noble assignments

---

## Group 5: Trading system (medium risk — touches stocks_queue.cpp)

### aff68cc — trading state management
- Files: `population.cpp`, `population.h`, `stocks_manager.cpp`, `stocks_queue.cpp`, `trade.h`, `trade_helpers.cpp`, `trade_manager.cpp`
- stocks_queue.cpp: changes crafts case to force stone, adds caravan multiplier
- stocks_manager.cpp: adds 1 line (minor)
- trade_manager.cpp: 260 lines of trading logic
- trade_helpers.cpp: caravan detection helpers
- trade.h: adds `caravan_is_near()` declaration

---

## Group 6: Manager order / chair bootstrap changes (HIGH RISK — this is where the regression lives)

### 32c0a17 — direct chair order creation
- Files: `stocks_queue.cpp`
- Changes chair case to queue directly when manager has no office

### c7eee44 — remove ManagerOrderExclusive
- Files: `stocks.h`, `stocks_equipment.cpp`, `stocks_forge.cpp`, `stocks_manager.cpp`, `stocks_queue.cpp`
- Removes ManagerOrderExclusive class and all `events.each_exclusive` calls

### 51f8880 — add exclusive_callback.h include
- Files: `stocks.h`
- Build fix for stocks.h

### a9083f7 — set default values on manager orders
- Files: `stocks_manager.cpp`, `stocks_queue.cpp`
- Adds frequency/workshop_id/max_workshops to order creation

### bfcd1b3 — set validated/active on manager orders
- Files: `stocks_manager.cpp`, `stocks_queue.cpp`
- Sets `status.bits.validated = true` and `status.bits.active = true`

### 7a5e531 — civzone creation + workshop job assignment
- Files: `plan_construct.cpp`, `stocks_manager.cpp`, `stocks_queue.cpp`
- plan_construct.cpp: adds civzone creation for Steam DF rooms (KEEP THIS)
- stocks_manager.cpp: adds Job::checkBuildingsNow()
- stocks_queue.cpp: major rewrite of chair bootstrap

### 5f2cb52 — fix owner assignment in try_endfurnish
- Files: `plan_construct.cpp`, `stocks_queue.cpp`
- plan_construct.cpp: null check for owner in civzone creation
- stocks_queue.cpp: parameter rename fixes

### 16c3a75 — add unit.h include
- Files: `plan_construct.cpp`
- Build fix: `#include "df/unit.h"` needed for civzone owner code

### c410a71 — refactor workshop job creation
- Files: `stocks_queue.cpp`
- Rewrites job creation logic for mason workshop bootstrap

### dec25ec — re-add ManagerOrderExclusive
- Files: `stocks.h`, `stocks_equipment.cpp`, `stocks_forge.cpp`, `stocks_manager.cpp`, `stocks_queue.cpp`
- Re-implements ManagerOrderExclusive (attempted fix of c7eee44)

### a19edbc — cleanup ManagerOrderExclusive
- Files: `stocks_manager.cpp`
- Removes unused Job module include

---

## Recommended re-apply order

1. **Group 2** (null-check fixes) — pure safety, no behavior change
2. **Group 3** (pause fix) — isolated
3. **Group 1** (camera) — isolated from stocks/plan logic
4. **Group 4** (noble assignment) — could affect manager appointment
5. **Group 5** (trading) — touches stocks_queue but only the crafts case
6. **Group 6** (manager/chair) — apply pieces individually, test after each:
   - First: civzone creation from 7a5e531 (plan_construct.cpp only) + 16c3a75 + 5f2cb52 (plan_construct.cpp only)
   - Then: validated/active bits (if needed)
   - Then: other pieces one at a time
